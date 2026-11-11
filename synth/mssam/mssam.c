/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * mssam: our own synthesiser for the Microsoft Sam, Mike and Mary voices
 * (and Mike and Mary for telephone).
 *
 * It reads the voice data (fetched by tools/fetch-mssam.py) and speaks with
 * it: each sound of the text is given a recorded unit of the voice, picked
 * by the voice's own context trees, and the units are joined with new pitch
 * and timing.  No Microsoft code is used or run.  docs/mssam-format.md
 * describes the data.
 *
 *   mssam [-d DIR] [-v sam|mike|mary|mike-tel|mary-tel] [-r RATE] [-p PITCH] [-o OUT.wav] TEXT...
 *
 * RATE and PITCH are multipliers (1 is the voice's own).  Without -o the
 * WAV goes to standard output.
 */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexicon.h"

#ifndef VOICEDIR
#define VOICEDIR "/usr/local/share/mssam"
#endif

#define MAXORDER 24		/* LPC order: 20, or 12 for the telephone voices */
#define ORDER V.order
#define MAXCB 16
#define MAXPH 64

static void die(const char *fmt, const char *arg)
{
	fprintf(stderr, "mssam: ");
	fprintf(stderr, fmt, arg);
	fputc('\n', stderr);
	exit(1);
}

static void *xmalloc(size_t n)
{
	void *p = calloc(1, n ? n : 1);
	if (!p)
		die("%s", "out of memory");
	return p;
}

static uint8_t *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *d;
	long n;

	if (!f)
		die("cannot open %s (run tools/fetch-mssam.py)", path);
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	d = xmalloc(n + 1);
	if (fread(d, 1, n, f) != (size_t)n)
		die("cannot read %s", path);
	fclose(f);
	*len = n;
	return d;
}

static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
	return le32(p) | (uint64_t)le32(p + 4) << 32;
}

static float lef(const uint8_t *p)
{
	uint32_t u = le32(p);
	float f;
	memcpy(&f, &u, 4);
	return f;
}

/* ---- the voice ---------------------------------------------------- */

struct table { int cnt, dim; const uint8_t *v; };	/* cnt vectors of dim floats */

struct node { int32_t yes, no; uint32_t q0, nq; };

struct phone { char name[9]; uint32_t first, nnodes, node0; };

static struct {
	int rate, order, period;	/* sample rate, LPC order, points in a stored pitch period */
	int nlsf, na, nb;
	struct table lsf[MAXCB], cba[MAXCB], cbb[MAXCB];
	uint32_t nunits;
	const uint8_t *units;		/* nunits+1 offsets, then the units */
	const uint8_t *ubase;
	size_t invlen;

	int nph, nq;
	struct phone ph[MAXPH];
	const uint8_t *q;		/* nq pairs of masks */
	const uint8_t *nodes;
	const uint8_t *qlist;

	float tone[4][2];		/* pitch at the start and end of a vowel */
} V;

enum { T_ACCENT, T_PLAIN, T_LASTACCENT, T_LAST };

static const uint8_t *read_table(const uint8_t *p, struct table *t)
{
	t->cnt = le32(p);
	t->dim = le32(p + 4);
	t->v = p + 8;
	return p + 8 + 4 * t->cnt * t->dim;
}

static void load_inventory(const char *path)
{
	const uint8_t *d = slurp(path, &V.invlen), *p;
	int i, s;

	V.rate = le32(d);
	V.order = le32(d + 4);
	if (V.order < 2 || V.order > MAXORDER || V.order % 2)
		die("%s: not a voice this program knows", path);
	V.nlsf = le32(d + 8);
	V.na = le32(d + 12);
	V.nb = le32(d + 16);
	if (V.nlsf > MAXCB || V.na > MAXCB || V.nb > MAXCB)
		die("%s: too many codebooks", path);
	p = d + 28;
	for (i = 0, s = 0; i < V.nlsf; i++) {
		p = read_table(p, &V.lsf[i]);
		s += V.lsf[i].dim;
	}
	if (s != ORDER)
		die("%s: line spectrum codebooks do not add up", path);
	for (i = 0, s = 0; i < V.na; i++) {
		p = read_table(p, &V.cba[i]);
		s += V.cba[i].dim;
	}
	V.period = s;
	for (i = 0, s = 0; i < V.nb; i++) {
		p = read_table(p, &V.cbb[i]);
		s += V.cbb[i].dim;
	}
	if (s != V.period)
		die("%s: excitation codebooks do not add up", path);
	V.nunits = le32(p);
	V.units = p + 8;
	V.ubase = V.units + 4 * (V.nunits + 1);
}

static void load_tree(const char *path)
{
	size_t len;
	const uint8_t *d = slurp(path, &len), *p;
	int i;

	if (len < 24 || memcmp(d, "MSSAMTR1", 8))
		die("%s: not a tree file from tools/fetch-mssam.py", path);
	V.nph = le32(d + 8);
	V.nq = le32(d + 12);
	if (V.nph > MAXPH)
		die("%s: too many sounds", path);
	V.q = d + 24;
	p = V.q + 16 * V.nq;
	for (i = 0; i < V.nph; i++, p += 20) {
		memcpy(V.ph[i].name, p, 8);
		V.ph[i].first = le32(p + 8);
		V.ph[i].nnodes = le32(p + 12);
		V.ph[i].node0 = le32(p + 16);
	}
	V.nodes = p + 8;
	V.qlist = V.nodes + 16 * le32(p);
}

/* The tone table: lines like "H (179) on: m=129= sd=34=; off: m=141= sd=21=" */
static void load_tones(const char *path)
{
	static const char *names[4] = { "H", "_", "h]", "_]" };
	static const float dflt[4][2] = { { 129, 141 }, { 112, 109 }, { 104, 86 }, { 92, 81 } };
	FILE *f = fopen(path, "r");
	char line[256], name[16];
	float on, off;
	int i;

	memcpy(V.tone, dflt, sizeof(V.tone));
	if (!f)
		return;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "%15s (%*d) on: m=%f= sd=%*f=; off: m=%f=", name, &on, &off) == 3)
			for (i = 0; i < 4; i++)
				if (!strcmp(name, names[i])) {
					V.tone[i][0] = on;
					V.tone[i][1] = off;
				}
	fclose(f);
}

static int phone_index(const char *name)
{
	int i;
	for (i = 0; i < V.nph; i++)
		if (!strcmp(V.ph[i].name, name))
			return i;
	return -1;
}

/* ---- picking units ------------------------------------------------ */

enum { POS_BEGIN, POS_END, POS_SINGLE, POS_INSIDE };

/* the unit for sound p, at word position pos, between sounds l and r */
static int pick_unit(int p, int pos, int l, int r)
{
	const struct phone *ph = &V.ph[p];
	uint32_t k = 0, i;

	if (!ph->nnodes)
		return -1;
	for (;;) {
		const uint8_t *n = V.nodes + 16 * (ph->node0 + k);
		int32_t yes = (int32_t)le32(n), no = (int32_t)le32(n + 4);
		uint32_t q0 = le32(n + 8), nq = le32(n + 12);
		int hit = 0;

		if (yes < 0)
			return ph->first + no + 1;	/* unit 0 is not a sound */
		for (i = 0; i < nq && !hit; i++) {
			const uint8_t *q = V.q + 16 * (V.qlist[2 * (q0 + i)] | V.qlist[2 * (q0 + i) + 1] << 8);
			uint64_t left = le64(q), right = le64(q + 8);
			hit = (left >> l & 1) && (left >> (60 + pos) & 1) && (right >> r & 1);
		}
		k = hit ? (uint32_t)yes : (uint32_t)no;
		if (k >= ph->nnodes)
			return -1;
	}
}

/* ---- decoding units ----------------------------------------------- */

struct frame {
	int voiced, len, orig;
	float gain, lsf[MAXORDER];
	float *period;			/* V.period points, for voiced frames */
};

/* Decode unit k into *out (a malloc'd array); returns the frame count. */
static int decode_unit(uint32_t k, struct frame **out)
{
	uint32_t a, b, n, i, j, c;
	const uint8_t *u, *end;
	struct frame *f;
	float *cur = NULL;

	*out = NULL;
	if (k >= V.nunits)
		return 0;
	a = le32(V.units + 4 * k);
	b = le32(V.units + 4 * (k + 1));
	if (b < a + 4 || V.ubase + b > (const uint8_t *)V.units + V.invlen)
		return 0;
	u = V.ubase + a;
	end = V.ubase + b;
	n = le32(u);
	u += 4;
	f = xmalloc(n * sizeof(*f));
	/* frame headers: a voiced frame is its pitch period in samples, as
	 * 0x7f bytes (127 each) and a last byte below 0x7f; an unvoiced frame
	 * is 0x80 bytes and a last byte above 0x80, its length */
	for (i = 0; i < n; i++) {
		if (u >= end)
			goto bad;
		if (*u <= 0x7f) {
			for (c = 0; *u == 0x7f; u++)
				c++;
			f[i].voiced = 1;
			f[i].len = 127 * c + *u++;
		} else {
			for (c = 0; *u == 0x80; u++)
				c++;
			f[i].len = 128 * (c ? c - 1 : 0) + *u++;
			/* in the 8 kHz voices this byte does not fit as a length;
			 * 5 ms frames match the units' recorded lengths best */
			if (V.rate < 16000)
				f[i].len = V.rate / 200;
		}
	}
	/* then a line spectrum codebook index per codebook per frame */
	if (u + (V.nlsf + 4) * n > end)
		goto bad;
	for (i = 0; i < n; i++) {
		int o = 0;
		for (j = 0; j < (uint32_t)V.nlsf; j++) {
			const struct table *t = &V.lsf[j];
			int idx = u[V.nlsf * i + j] % t->cnt, d;
			for (d = 0; d < t->dim; d++)
				f[i].lsf[o++] = lef(t->v + 4 * (idx * t->dim + d));
		}
	}
	u += V.nlsf * n;
	for (i = 0; i < n; i++)
		f[i].gain = lef(u + 4 * i);
	u += 4 * n;
	/* then the excitation of each voiced frame: the first frame of a voiced
	 * run holds a whole pitch period (codebooks A), each later one holds the
	 * change from the frame before (codebooks B) */
	for (i = 0; i < n; i++) {
		const struct table *cb;
		int ncb, o = 0, d;
		float *per;

		if (!f[i].voiced) {
			cur = NULL;
			continue;
		}
		cb = cur ? V.cbb : V.cba;
		ncb = cur ? V.nb : V.na;
		if (u + ncb > end)
			goto bad;
		per = xmalloc(V.period * sizeof(float));
		if (cur)
			memcpy(per, cur, V.period * sizeof(float));
		for (j = 0; j < (uint32_t)ncb; j++) {
			int idx = u[j] % cb[j].cnt;
			for (d = 0; d < cb[j].dim; d++, o++)
				per[o] += lef(cb[j].v + 4 * (idx * cb[j].dim + d));
		}
		u += ncb;
		f[i].period = cur = per;
	}
	*out = f;
	return n;
bad:
	for (i = 0; i < n; i++)
		free(f[i].period);
	free(f);
	return 0;
}

/* ---- text to sounds ----------------------------------------------- */

struct sound {
	int ph, pos, vowel;	/* vowel: 0, or 1 + the tone */
	float f0[2];		/* a vowel's pitch where it starts and ends, Hz */
	int pause;		/* > 0: a pause of this many ms instead of a sound */
};

static struct sound snd[4096];
static int nsnd;

/* cmudict.dict, if there is one: its lines, sorted by word */
static char **dict;
static size_t ndict;

static int wordcmp(const char *a, const char *b)
{
	for (; *a && *a != ' ' && *a == *b; a++, b++)
		;
	return (unsigned char)(*a == ' ' ? 0 : *a) - (unsigned char)(*b == ' ' ? 0 : *b);
}

static int linecmp(const void *a, const void *b)
{
	return wordcmp(*(char *const *)a, *(char *const *)b);
}

static void load_dict(const char *path)
{
	size_t len, i, n = 0;
	char *d = (char *)slurp(path, &len), *p;

	for (i = 0; i < len; i++)
		n += d[i] == '\n';
	dict = xmalloc((n + 1) * sizeof(char *));
	for (p = strtok(d, "\n"); p; p = strtok(NULL, "\n")) {
		char *sp = strchr(p, ' '), *hash = strchr(p, '#');
		if (!sp || p[0] == ';' || ndict > n)
			continue;
		if (hash)
			*hash = 0;	/* a comment */
		if (sp > p && sp[-1] == ')')
			continue;	/* other pronunciations: word(2) */
		dict[ndict++] = p;
	}
	qsort(dict, ndict, sizeof(char *), linecmp);
}

static const char *dict_lookup(const char *w)
{
	size_t lo = 0, hi = ndict;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int c = wordcmp(dict[mid], w);
		if (!c)
			return strchr(dict[mid], ' ') + 1;
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return NULL;
}

static int lexcmp(const void *a, const void *b)
{
	return strcmp(a, *(const char *const *)b);
}

static const char *letters[26] = {
	"EY1", "B IY1", "S IY1", "D IY1", "IY1", "EH1 F", "JH IY1", "EY1 CH", "AY1",
	"JH EY1", "K EY1", "EH1 L", "EH1 M", "EH1 N", "OW1", "P IY1", "K Y UW1",
	"AA1 R", "EH1 S", "T IY1", "Y UW1", "V IY1", "D AH1 B AH0 L Y UW0",
	"EH1 K S", "W AY1", "Z IY1",
};

static const char *pronounce(const char *w)
{
	const void *e = bsearch(w, lexicon, sizeof(lexicon) / sizeof(lexicon[0]),
				sizeof(lexicon[0]), lexcmp);
	if (e)
		return ((const char *const *)e)[1];
	return dict_lookup(w);
}

static int content_word(const char *w)
{
	static const char *fn[] = { "a", "an", "and", "at", "be", "by", "for", "in", "is", "it",
		"it's", "its", "of", "on", "or", "the", "to", "will", NULL };
	int i;
	for (i = 0; fn[i]; i++)
		if (!strcmp(w, fn[i]))
			return 0;
	return 1;
}

static void add_pause(int ms)
{
	if (nsnd && snd[nsnd - 1].pause)
		snd[nsnd - 1].pause = ms > snd[nsnd - 1].pause ? ms : snd[nsnd - 1].pause;
	else if (nsnd < (int)(sizeof(snd) / sizeof(snd[0])))
		snd[nsnd++] = (struct sound){ .ph = -1, .pause = ms };
}

/* The sounds of one word, from its phones ("AE1 T"). */
static void add_word(const char *w, const char *phones, int content)
{
	char buf[256], *t, *save;
	int first = nsnd;

	snprintf(buf, sizeof(buf), "%s", phones);
	for (t = strtok_r(buf, " \t\r\n", &save); t; t = strtok_r(NULL, " \t\r\n", &save)) {
		char name[16];
		size_t l = strlen(t);
		int stress = -1, p;

		if (l >= sizeof(name) - 2 || nsnd >= (int)(sizeof(snd) / sizeof(snd[0])))
			break;
		memcpy(name, t, l + 1);
		if (isdigit((unsigned char)name[l - 1])) {
			stress = name[l - 1] - '0';
			name[l - 1] = stress ? '1' : '0';	/* the voices have stress 1 and 0 */
		}
		p = phone_index(name);
		if (p < 0 && stress >= 0) {
			name[l - 1] = '1';
			p = phone_index(name);
		}
		if (p < 0)
			continue;
		snd[nsnd++] = (struct sound){ .ph = p,
			.vowel = stress < 0 ? 0 : 1 + (stress && content ? T_ACCENT : T_PLAIN) };
	}
	(void)w;
	for (int i = first; i < nsnd; i++)
		snd[i].pos = nsnd - first == 1 ? POS_SINGLE : i == first ? POS_BEGIN :
			     i == nsnd - 1 ? POS_END : POS_INSIDE;
}

static void say_word(const char *w)
{
	const char *ph = pronounce(w);
	char one[2] = { 0, 0 };

	if (ph) {
		add_word(w, ph, content_word(w));
		return;
	}
	/* not known: spell it */
	for (; *w; w++)
		if (*w >= 'a' && *w <= 'z') {
			one[0] = *w;
			add_word(one, letters[*w - 'a'], 1);
		}
}

static void say_number(long n, char *out, size_t sz);

static const char *ones[] = { "zero", "one", "two", "three", "four", "five", "six", "seven",
	"eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
	"sixteen", "seventeen", "eighteen", "nineteen" };
static const char *tens[] = { "", "", "twenty", "thirty", "forty", "fifty", "sixty",
	"seventy", "eighty", "ninety" };

static void say_number(long n, char *out, size_t sz)
{
	size_t l = strlen(out);
	if (n >= 1000000) {
		say_number(n / 1000000, out, sz);
		l = strlen(out);
		snprintf(out + l, sz - l, " million");
		if (n % 1000000)
			say_number(n % 1000000, out, sz);
	} else if (n >= 1000) {
		say_number(n / 1000, out, sz);
		l = strlen(out);
		snprintf(out + l, sz - l, " thousand");
		if (n % 1000)
			say_number(n % 1000, out, sz);
	} else if (n >= 100) {
		snprintf(out + l, sz - l, " %s hundred", ones[n / 100]);
		if (n % 100) {
			l = strlen(out);
			snprintf(out + l, sz - l, " and");
			say_number(n % 100, out, sz);
		}
	} else if (n >= 20) {
		snprintf(out + l, sz - l, " %s", tens[n / 10]);
		if (n % 10)
			say_number(n % 10, out, sz);
	} else {
		snprintf(out + l, sz - l, " %s", ones[n]);
	}
}

/* The pitch of a phrase, in Sam's Hz (other voices are scaled to theirs):
 * it starts near TOP and sinks to BOTTOM, the first accented word stands
 * out by ACCENT1 and later ones by ACCENT2, and the last vowel falls to
 * END.  Measured from Sam's own recordings of the clock's phrases. */
static const double TOP = 100, BOTTOM = 82, ACCENT1 = 22, ACCENT2 = 10, END = 73;

static void end_phrase(int from)
{
	int i, nv = 0, j = 0, accents = 0;

	for (i = from; i < nsnd; i++)
		nv += snd[i].vowel != 0;
	for (i = from; i < nsnd; i++) {
		double x0, x1, b0, b1, lift = 0;
		if (!snd[i].vowel)
			continue;
		x0 = nv > 1 ? (double)j / nv : 0;
		x1 = nv > 1 ? (j + 1.0) / nv : 1;
		b0 = TOP - (TOP - BOTTOM) * x0;
		b1 = TOP - (TOP - BOTTOM) * x1;
		if (snd[i].vowel == 1 + T_ACCENT)
			lift = accents++ ? ACCENT2 : ACCENT1;
		snd[i].f0[0] = b0 + lift;
		snd[i].f0[1] = b1 + lift * 0.6;
		if (++j == nv) {
			snd[i].f0[0] = b0 + lift * 0.5;
			snd[i].f0[1] = END;
		}
	}
}

static void parse_text(const char *text)
{
	char word[64], words[512];
	const char *s = text;
	int phrase = 0;

	add_pause(120);
	while (*s) {
		size_t l = 0;

		if (isdigit((unsigned char)*s)) {
			/* a number; after a ':' a minute ("05" is "oh five") */
			long n = 0;
			int digits = 0, minute = s > text && s[-1] == ':';
			while (isdigit((unsigned char)*s) && digits < 9)
				n = n * 10 + (*s++ - '0'), digits++;
			words[0] = 0;
			if (minute && n == 0)
				snprintf(words, sizeof(words), " hundred");
			else if (minute && n < 10)
				snprintf(words, sizeof(words), " oh %s", ones[n]);
			else
				say_number(n, words, sizeof(words));
			for (char *t = strtok(words, " "); t; t = strtok(NULL, " "))
				say_word(t);
			continue;
		}
		if (isalpha((unsigned char)*s)) {
			while ((isalpha((unsigned char)*s) || *s == '\'') && l < sizeof(word) - 1)
				word[l++] = tolower((unsigned char)*s++);
			word[l] = 0;
			/* "a.m.", "p.m." and the like: letters */
			if (l == 1 && *s == '.' && isalpha((unsigned char)s[1]) && s[2] == '.') {
				say_word(word);
				word[0] = tolower((unsigned char)s[1]);
				s += 3;
				add_word(word, letters[word[0] - 'a'], 1);
				continue;
			}
			if (l == 1 && word[0] != 'a' && word[0] != 'i')
				add_word(word, letters[word[0] - 'a'], 1);
			else
				say_word(word);
			continue;
		}
		if (strchr(",;:", *s) && !(*s == ':' && isdigit((unsigned char)s[1]))) {
			end_phrase(phrase);
			add_pause(180);
			phrase = nsnd;
		} else if (strchr(".!?", *s)) {
			end_phrase(phrase);
			add_pause(350);
			phrase = nsnd;
		}
		s++;
	}
	end_phrase(phrase);
	add_pause(120);
}

/* ---- speaking ----------------------------------------------------- */

static void lsf_to_lpc(const float *lsf, double *a)
{
	double p[MAXORDER + 2] = { 1 }, q[MAXORDER + 2] = { 1 };
	int np = 1, nq = 1, i, j;

	for (i = 0; i < ORDER; i++) {
		double c = -2 * cos(2 * M_PI * lsf[i]), *v = i % 2 ? q : p;
		int *n = i % 2 ? &nq : &np;
		for (j = *n + 1; j >= 1; j--)
			v[j] += c * v[j - 1] + (j >= 2 ? v[j - 2] : 0);
		*n += 2;
	}
	/* p *= (1 + z^-1), q *= (1 - z^-1) */
	for (j = np; j >= 1; j--)
		p[j] += p[j - 1];
	for (j = nq; j >= 1; j--)
		q[j] -= q[j - 1];
	for (i = 0; i <= ORDER; i++)
		a[i] = 0.5 * (p[i] + q[i]);
}

static unsigned rng = 1;
static double noise(void)
{
	/* sum of uniforms: near enough to Gaussian for frication */
	double s = 0;
	int i;
	for (i = 0; i < 4; i++) {
		rng = rng * 1103515245 + 12345;
		s += (rng >> 8 & 0xffff) / 65536.0 - 0.5;
	}
	return s * 1.732;
}

static float *out;
static size_t nout, aout;

static void emit(double v)
{
	if (nout == aout) {
		aout = aout ? 2 * aout : 1 << 16;
		out = realloc(out, aout * sizeof(*out));
		if (!out)
			die("%s", "out of memory");
	}
	out[nout++] = v;
}

/* One stored period (V.period points) at its real length n.  It is one
 * period of a periodic signal, so it is resampled through its spectrum,
 * keeping the harmonics that fit below the new Nyquist frequency. */

static void stretch(const float *p, double *e, int n)
{
	static double re[1024], im[1024];
	int N = V.period, h = n / 2 < N / 2 ? n / 2 : N / 2, k, j;

	for (k = 0; k <= h; k++) {
		double a = 0, b = 0;
		for (j = 0; j < N; j++) {
			double w = 2 * M_PI * k * j / N;
			a += p[j] * cos(w);
			b -= p[j] * sin(w);
		}
		re[k] = a;
		im[k] = b;
	}
	for (j = 0; j < n; j++) {
		double v = re[0];
		for (k = 1; k <= h; k++) {
			double w = 2 * M_PI * k * j / n, c = k == h && 2 * h == n ? 1 : 2;
			v += c * (re[k] * cos(w) - im[k] * sin(w));
		}
		e[j] = v / N;
	}
}

/* The loudness of a unit's first frames leans toward the end of the unit
 * before, so that joins do not jump. */
static const double JOIN = 0.6;
/* Each pitch pulse is blended with the one before: the stored pulses vary
 * more from one to the next than Sam's own speech does, which sounds rough. */
static const double SMOOTH = 0.7;
static float sper[1024];
static int have_per;
static double last_gain;

static double gain_at(double g, int k)
{
	double w = k == 0 ? JOIN : k == 1 ? JOIN / 2 : 0;
	if (w <= 0 || last_gain <= 0 || g <= 0)
		return g;
	return exp((1 - w) * log(g) + w * log(last_gain));
}

static void render(double rate, double pitch)
{
	/* the voice's own pitch against Sam's (plain syllables: 112 Hz) */
	double vscale = V.tone[T_PLAIN][0] / 112.0;
	static double mem[MAXORDER], prev_y;
	static float last[MAXORDER];
	static int have_last;
	double a[MAXORDER + 1], *e = NULL;
	int i, j, k, ne = 0;

	for (i = 0; i < nsnd; i++) {
		struct frame *f;
		double f0a, f0b, total, t, *src;
		int n, l, r, unit, prev, next;

		if (snd[i].pause) {
			for (k = 0; k < snd[i].pause * V.rate / 1000; k++)
				emit(0);
			memset(mem, 0, sizeof(mem));
			prev_y = 0;
			have_last = 0;
			last_gain = 0;
			continue;
		}
		/* the sounds either side; a pause counts as silence */
		l = i > 0 && !snd[i - 1].pause ? snd[i - 1].ph : -1;
		r = i + 1 < nsnd && !snd[i + 1].pause ? snd[i + 1].ph : -1;
		if (l < 0)
			l = phone_index("SILb") >= 0 ? phone_index("SILb") : phone_index("SIL");
		if (r < 0)
			r = phone_index("SILe") >= 0 ? phone_index("SILe") : phone_index("SIL");
		unit = pick_unit(snd[i].ph, snd[i].pos, l < 0 ? 0 : l, r < 0 ? 0 : r);
		n = unit < 0 ? 0 : decode_unit(unit, &f);
		if (!n)
			continue;
		/* pitch: a vowel goes from its tone's start to its end; the
		 * sounds between vowels glide from one to the next */
		for (prev = i - 1; prev >= 0 && !snd[prev].pause && !snd[prev].vowel; prev--)
			;
		for (next = i + 1; next < nsnd && !snd[next].pause && !snd[next].vowel; next++)
			;
		if (snd[i].vowel) {
			f0a = snd[i].f0[0];
			f0b = snd[i].f0[1];
		} else {
			int pv = prev >= 0 && snd[prev].vowel, nv = next < nsnd && snd[next].vowel;
			f0a = pv ? snd[prev].f0[1] : nv ? snd[next].f0[0] : TOP;
			f0b = nv ? snd[next].f0[0] : f0a;
		}
		f0a *= vscale;
		f0b *= vscale;
		/* new timing: step through the unit's frames at the new rate */
		src = xmalloc((n + 1) * sizeof(double));
		for (k = 0; k < n; k++)
			src[k + 1] = src[k] + f[k].len;
		total = src[n] / rate;
		for (t = 0; t < total;) {
			struct frame *s;
			int len, L0, sf;
			double g, sum = 0;

			for (k = 0; k < n - 1 && src[k + 1] <= t * rate; k++)
				;
			s = &f[k];
			sf = k;
			L0 = s->len;
			if (s->voiced) {
				len = (int)lrint(V.rate / ((f0a + (f0b - f0a) * t / total) * pitch));
			} else {
				len = L0;
			}
			if (len < 20)
				len = 20;
			if (ne < (L0 > len ? L0 : len)) {
				ne = L0 > len ? L0 : len;
				e = realloc(e, ne * sizeof(double));
			}
			if (s->voiced && s->period) {
				/* the stored period is scaled to V.period points:
				 * stretch it back to its recorded length, then cut
				 * or pad it to the new period */
				if (SMOOTH > 0 && have_per) {
					for (k = 0; k < V.period; k++)
						sper[k] = SMOOTH * sper[k] + (1 - SMOOTH) * s->period[k];
				} else {
					memcpy(sper, s->period, V.period * sizeof(float));
				}
				have_per = 1;
				stretch(sper, e, L0);
				for (k = 0; k < L0; k++)
					sum += e[k] * e[k];
				for (; k < len; k++)
					e[k] = 0;
				g = gain_at(s->gain, sf) / (sqrt(sum / L0) + 1e-9);
			} else {
				have_per = 0;
				for (k = 0; k < len; k++) {
					e[k] = noise();
					sum += e[k] * e[k];
				}
				g = gain_at(s->gain, sf) / (sqrt(sum / len) + 1e-9);
			}
			/* the spectrum glides from the last frame's to this one's,
			 * in steps of 32 samples, so frames do not click */
			if (!have_last)
				memcpy(last, s->lsf, sizeof(last));
			for (k = 0; k < len; k++) {
				double y = e[k] * g;
				if (k % 32 == 0) {
					float l[MAXORDER];
					double w = (k + 16.0) / len;
					if (w > 1)
						w = 1;
					for (j = 0; j < ORDER; j++)
						l[j] = (1 - w) * last[j] + w * s->lsf[j];
					lsf_to_lpc(l, a);
				}
				for (j = 0; j < ORDER; j++)
					y -= a[j + 1] * mem[j];
				memmove(mem + 1, mem, (ORDER - 1) * sizeof(double));
				mem[0] = y;
				/* the voices' spectra lack the treble of the recordings
				 * (about 10 dB above 3 kHz); this lift matches it */
				emit(y - 0.65 * prev_y);
				prev_y = y;
			}
			memcpy(last, s->lsf, sizeof(last));
			have_last = 1;
			t += len;
		}
		free(src);
		last_gain = f[n - 1].gain;
		for (k = 0; k < n; k++)
			free(f[k].period);
		free(f);
	}
	free(e);
}

static void put32(FILE *f, uint32_t v)
{
	uint8_t b[4] = { v, v >> 8, v >> 16, v >> 24 };
	fwrite(b, 1, 4, f);
}

static void put16(FILE *f, uint16_t v)
{
	uint8_t b[2] = { v, v >> 8 };
	fwrite(b, 1, 2, f);
}

/* Narrow the gap between loud vowels and quiet consonants: the level,
 * followed over about 10 ms, is raised to the power 1 - COMPRESS. */
static const double COMPRESS = 0.2;

static void compress(void)
{
	size_t i, w = V.rate / 100;
	double env = 0, top = 1e-9, *lev;

	if (COMPRESS <= 0 || !nout)
		return;
	lev = xmalloc(nout * sizeof(double));
	for (i = 0; i < nout; i++) {
		env += (out[i] * out[i] - env) / w;
		lev[i] = sqrt(env);
		if (lev[i] > top)
			top = lev[i];
	}
	/* the same backwards, so the level does not lag */
	for (env = lev[nout - 1] * lev[nout - 1], i = nout; i-- > 0;) {
		env += (out[i] * out[i] - env) / w;
		lev[i] = (lev[i] + sqrt(env)) / 2;
	}
	for (i = 0; i < nout; i++) {
		double l = lev[i] / top;
		if (l > 1e-3)
			out[i] *= pow(l, -COMPRESS);
		else
			out[i] *= pow(1e-3, -COMPRESS);
	}
	free(lev);
}

static void write_wav(FILE *f)
{
	double peak = 1e-9;
	size_t i;

	compress();
	for (i = 0; i < nout; i++)
		if (fabs(out[i]) > peak)
			peak = fabs(out[i]);
	fwrite("RIFF", 1, 4, f);
	put32(f, 36 + 2 * nout);
	fwrite("WAVEfmt ", 1, 8, f);
	put32(f, 16);
	put16(f, 1);
	put16(f, 1);
	put32(f, V.rate);
	put32(f, 2 * V.rate);
	put16(f, 2);
	put16(f, 16);
	fwrite("data", 1, 4, f);
	put32(f, 2 * nout);
	for (i = 0; i < nout; i++)
		put16(f, (uint16_t)(int16_t)lrint(out[i] / peak * 26000));
}

static void usage(void)
{
	fprintf(stderr, "usage: mssam [-d DIR] [-v VOICE] [-r RATE] [-p PITCH] [-o OUT.wav] TEXT...\n"
		"  DIR holds the voice data from tools/fetch-mssam.py (default %s)\n", VOICEDIR);
	exit(2);
}

int main(int argc, char **argv)
{
	const char *dir = getenv("MSSAM_DIR") ? getenv("MSSAM_DIR") : VOICEDIR;
	const char *voice = "sam", *outp = NULL;
	double rate = 0.82, pitch = 1;	/* Sam's pace */
	char path[4096], text[8192] = "";
	int i;
	FILE *f;

	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
		char opt = argv[i][1];
		if (opt == '-') {
			i++;
			break;
		}
		if (strchr("dvrpo", opt) && i + 1 >= argc)
			usage();
		switch (opt) {
		case 'd': dir = argv[++i]; break;
		case 'v': voice = argv[++i]; break;
		case 'r': rate *= atof(argv[++i]); break;
		case 'p': pitch *= atof(argv[++i]); break;
		case 'o': outp = argv[++i]; break;
		default: usage();
		}
	}
	if (i >= argc || rate <= 0.05 || pitch <= 0.05)
		usage();
	for (; i < argc; i++) {
		strncat(text, argv[i], sizeof(text) - strlen(text) - 2);
		strcat(text, " ");
	}
	if (strcmp(voice, "sam") && strcmp(voice, "mike") && strcmp(voice, "mary") &&
	    strcmp(voice, "mike-tel") && strcmp(voice, "mary-tel"))
		die("unknown voice %s (sam, mike, mary, mike-tel or mary-tel)", voice);
	if (strcmp(voice, "sam"))
		rate /= 0.82;

	snprintf(path, sizeof(path), "%s/%s.inv", dir, voice);
	load_inventory(path);
	snprintf(path, sizeof(path), "%s/%s.tree", dir, voice);
	load_tree(path);
	snprintf(path, sizeof(path), "%s/%s.tones", dir, voice);
	load_tones(path);
	snprintf(path, sizeof(path), "%s/cmudict.dict", dir);
	if ((f = fopen(path, "rb"))) {
		fclose(f);
		load_dict(path);
	}

	parse_text(text);
	render(rate, pitch);

	f = outp ? fopen(outp, "wb") : stdout;
	if (!f)
		die("cannot write %s", outp);
	write_wav(f);
	if (outp && fclose(f))
		die("cannot write %s", outp);
	return 0;
}
