/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * macintalk: a speech synthesiser that speaks with the MacInTalk 3 voices
 * (Fred, Ralph, Kathy, Zarvox ...) of classic Mac OS.
 *
 * This is a clean-room program.  It contains no Apple code and does not
 * run, emulate or load any.  It reads only the voices' data, the 'ttvd'
 * (and 'ttvw') resources of Apple's voice files, which
 * tools/fetch-macintalk.py takes from a Mac OS 8.1 disk image at build
 * time.  What each field of a voice means was worked out by comparing the
 * 19 voices (docs/macintalk-format.md).  The rest is our own: the NRL
 * letter-to-sound rules (rules.h, public domain), the prosody, and a
 * formant synthesiser in the style of D. H. Klatt's 1980 design.  The voice
 * gives the synthesiser its pitch, speed, glottal source spectrum, formant
 * and bandwidth settings, breathiness, chorus, melody and sampled sound.
 *
 *   macintalk [-v voice] [-d voicedir] [-o out.wav] [-r wpm] [-p hz] text...
 *   macintalk -l [-d voicedir]          list the voices
 *   macintalk -x [-v voice] text...     print the phonemes, make no sound
 *
 * The text may hold Speech Manager commands: [[inpt PHON]] (Apple's
 * phoneme symbols follow) and [[inpt TEXT]], [[rate N]], [[pbas N]],
 * [[slnc N]].  Without -o the WAV (22050 Hz, 16-bit, mono) goes to standard
 * output.
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rules.h"

#define FS 22050
#define NHARM 47
#define MAXSEG 4096
#define BLOCK 32            /* samples between parameter updates */

static double frand(void)
{
	static uint32_t s = 22222;
	s = s * 1103515245u + 12345u;
	return ((s >> 9) & 0xffff) / 32768.0 - 1.0;
}

/* ---- phonemes ------------------------------------------------------------ */

enum { V = 1, DIPH = 2, SON = 4, NAS = 8, FRIC = 16, STOP = 32, AFFR = 64, VOI = 128, ASP = 256 };

struct phon {
	const char *name;
	int flags;
	double f1, f2, f3, e1, e2, e3;  /* formants, and where a diphthong ends */
	double b1, b2, b3;
	double dur;                     /* ms at 170 wpm */
	double av, af;                  /* voicing and frication, dB */
	double fc, fbw;                 /* frication (or burst) noise band */
};

static const struct phon ph[] = {
	{ "_", 0, 500, 1500, 2500, 0, 0, 0, 100, 100, 100, 100, 0, 0, 0, 0 },
	{ "IY", V | VOI, 310, 2020, 2960, 0, 0, 0, 45, 200, 400, 155, 60, 0, 0, 0 },
	{ "IH", V | VOI, 400, 1800, 2570, 0, 0, 0, 50, 100, 140, 125, 60, 0, 0, 0 },
	{ "EY", V | DIPH | VOI, 480, 1720, 2520, 330, 2200, 2600, 70, 100, 200, 185, 60, 0, 0, 0 },
	{ "EH", V | VOI, 530, 1680, 2500, 0, 0, 0, 60, 90, 200, 140, 60, 0, 0, 0 },
	{ "AE", V | VOI, 620, 1660, 2430, 0, 0, 0, 70, 150, 320, 210, 60, 0, 0, 0 },
	{ "AA", V | VOI, 700, 1220, 2600, 0, 0, 0, 130, 70, 160, 210, 60, 0, 0, 0 },
	{ "AO", V | VOI, 600, 990, 2570, 0, 0, 0, 90, 100, 80, 210, 60, 0, 0, 0 },
	{ "OW", V | DIPH | VOI, 540, 1100, 2300, 450, 900, 2300, 80, 70, 70, 200, 60, 0, 0, 0 },
	{ "UH", V | VOI, 450, 1100, 2350, 0, 0, 0, 80, 100, 80, 140, 60, 0, 0, 0 },
	{ "UW", V | DIPH | VOI, 350, 1250, 2200, 320, 900, 2200, 65, 110, 140, 190, 60, 0, 0, 0 },
	{ "AX", V | VOI, 500, 1400, 2300, 0, 0, 0, 100, 100, 200, 90, 58, 0, 0, 0 },
	{ "IX", V | VOI, 420, 1650, 2500, 0, 0, 0, 70, 100, 200, 90, 58, 0, 0, 0 },
	{ "AH", V | VOI, 620, 1220, 2550, 0, 0, 0, 80, 50, 140, 140, 60, 0, 0, 0 },
	{ "ER", V | VOI, 470, 1270, 1540, 0, 0, 0, 100, 60, 110, 170, 60, 0, 0, 0 },
	{ "AY", V | DIPH | VOI, 660, 1200, 2550, 400, 1880, 2500, 100, 70, 200, 240, 60, 0, 0, 0 },
	{ "AW", V | DIPH | VOI, 640, 1230, 2550, 420, 940, 2350, 80, 70, 140, 250, 60, 0, 0, 0 },
	{ "OY", V | DIPH | VOI, 550, 960, 2400, 360, 1820, 2450, 80, 50, 130, 260, 60, 0, 0, 0 },
	{ "l", SON | VOI, 330, 1050, 2400, 0, 0, 0, 50, 100, 280, 70, 54, 0, 0, 0 },
	{ "r", SON | VOI, 310, 1060, 1380, 0, 0, 0, 70, 100, 120, 70, 54, 0, 0, 0 },
	{ "w", SON | VOI, 290, 610, 2150, 0, 0, 0, 50, 80, 60, 70, 54, 0, 0, 0 },
	{ "y", SON | VOI, 260, 2070, 3020, 0, 0, 0, 40, 250, 500, 60, 54, 0, 0, 0 },
	{ "m", NAS | VOI, 480, 1270, 2130, 0, 0, 0, 40, 200, 200, 70, 52, 0, 0, 0 },
	{ "n", NAS | VOI, 480, 1340, 2470, 0, 0, 0, 40, 300, 300, 60, 52, 0, 0, 0 },
	{ "N", NAS | VOI, 480, 2000, 2700, 0, 0, 0, 160, 150, 100, 90, 52, 0, 0, 0 },
	{ "h", ASP, 500, 1500, 2500, 0, 0, 0, 200, 150, 200, 70, 0, 0, 0, 0 },
	{ "f", FRIC, 340, 1100, 2080, 0, 0, 0, 200, 120, 150, 100, 0, 44, 6000, 6000 },
	{ "v", FRIC | VOI, 220, 1100, 2080, 0, 0, 0, 60, 90, 120, 65, 48, 38, 6000, 6000 },
	{ "T", FRIC, 320, 1290, 2540, 0, 0, 0, 200, 90, 200, 90, 0, 42, 5500, 5000 },
	{ "D", FRIC | VOI, 270, 1290, 2540, 0, 0, 0, 60, 80, 170, 50, 50, 36, 5500, 5000 },
	{ "s", FRIC, 320, 1390, 2530, 0, 0, 0, 200, 80, 200, 110, 0, 50, 5200, 2000 },
	{ "z", FRIC | VOI, 240, 1390, 2530, 0, 0, 0, 70, 60, 180, 80, 48, 45, 5200, 2000 },
	{ "S", FRIC, 300, 1840, 2750, 0, 0, 0, 200, 100, 300, 110, 0, 57, 2900, 1800 },
	{ "Z", FRIC | VOI, 280, 1840, 2750, 0, 0, 0, 70, 100, 300, 80, 48, 50, 2900, 1800 },
	{ "p", STOP, 400, 1100, 2150, 0, 0, 0, 300, 150, 220, 80, 0, 50, 1200, 2500 },
	{ "b", STOP | VOI, 200, 1100, 2150, 0, 0, 0, 60, 110, 130, 65, 0, 45, 1200, 2500 },
	{ "t", STOP, 400, 1600, 2600, 0, 0, 0, 300, 120, 250, 70, 0, 56, 4200, 3000 },
	{ "d", STOP | VOI, 200, 1600, 2600, 0, 0, 0, 60, 100, 170, 60, 0, 50, 4200, 3000 },
	{ "k", STOP, 300, 1990, 2850, 0, 0, 0, 250, 160, 330, 80, 0, 54, 2300, 1500 },
	{ "g", STOP | VOI, 200, 1990, 2850, 0, 0, 0, 60, 150, 280, 65, 0, 50, 2300, 1500 },
	{ "C", AFFR, 350, 1800, 2820, 0, 0, 0, 200, 90, 300, 70, 0, 56, 2900, 1800 },
	{ "J", AFFR | VOI, 260, 1800, 2820, 0, 0, 0, 60, 80, 270, 60, 0, 50, 2900, 1800 },
};
#define NPH ((int)(sizeof(ph) / sizeof(ph[0])))

static int ph_find(const char *s, size_t n)
{
	int i;
	for (i = 1; i < NPH; i++)
		if (strlen(ph[i].name) == n && !strncmp(ph[i].name, s, n))
			return i;
	return -1;
}

/* Apple's phoneme symbols (Speech Manager, [[inpt PHON]]) */
static const struct { const char *apple, *ours; } apple_ph[] = {
	{ "AE", "AE" }, { "EY", "EY" }, { "AO", "AO" }, { "AX", "AX" }, { "IY", "IY" },
	{ "EH", "EH" }, { "IH", "IH" }, { "AY", "AY" }, { "IX", "IX" }, { "AA", "AA" },
	{ "UW", "UW" }, { "UH", "UH" }, { "UX", "AH" }, { "OW", "OW" }, { "AW", "AW" },
	{ "OY", "OY" }, { "b", "b" }, { "C", "C" }, { "d", "d" }, { "D", "D" },
	{ "f", "f" }, { "g", "g" }, { "h", "h" }, { "J", "J" }, { "k", "k" },
	{ "l", "l" }, { "m", "m" }, { "n", "n" }, { "N", "N" }, { "p", "p" },
	{ "r", "r" }, { "s", "s" }, { "S", "S" }, { "t", "t" }, { "T", "T" },
	{ "v", "v" }, { "w", "w" }, { "y", "y" }, { "z", "z" }, { "Z", "Z" },
};

/* ---- voices ---------------------------------------------------------------- */

struct voice {
	char name[64], comment[256];
	int16_t w[200];       /* the MacinTalk 3 parameter block, see below */
	int nw;
	double tab[2][NHARM]; /* two glottal source spectra */
	int nnotes;
	int note_dur[64], note_semi[64];
	float *wave;          /* the voice's sampled sound (ttvw), or NULL */
	int nwave, nseg, seg[16];
};

/* Fields of the parameter block (16-bit words after the Speech Manager
 * VoiceDescription) that this synthesiser uses.  docs/macintalk-format.md
 * lists all of them. */
enum {
	W_PITCH = 0, W_PMOD = 1, W_RATE = 3, W_BREATH = 6, W_VOICING = 7,
	W_F4 = 8, W_B4 = 9, W_F5 = 10, W_B5 = 11, W_FNP = 16, W_FNZ = 17,
	W_B1PCT = 20, W_B2PCT = 21, W_B3PCT = 22, W_DF1 = 23, W_DF2 = 24, W_DF3 = 25,
	W_CHORUS = 26, W_GAIN = 27, W_WAVEMODE = 46, W_TAB_A = 48, W_TAB_B = 96,
	W_NOTES = 174,
};

static int voice_load(struct voice *v, const char *dir, const char *id)
{
	char path[1024];
	unsigned char buf[4096];
	FILE *f;
	size_t n, i;

	memset(v, 0, sizeof(*v));
	snprintf(path, sizeof(path), "%s/%s.ttvd", dir, id);
	if (!(f = fopen(path, "rb")))
		return -1;
	n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	if (n < 362 + 2 * 176 || memcmp(buf + 4, "mtk3", 4)) {
		fprintf(stderr, "macintalk: %s: not a MacinTalk 3 voice\n", path);
		return -1;
	}
	/* VoiceDescription: length, VoiceSpec, version, Str63 name, Str255 comment ... */
	memcpy(v->name, buf + 17, buf[16] < 63 ? buf[16] : 63);
	memcpy(v->comment, buf + 81, buf[80]);
	v->nw = (int)((n - 362) / 2);
	if (v->nw > 200)
		v->nw = 200;
	for (i = 0; i < (size_t)v->nw; i++)
		v->w[i] = (int16_t)(buf[362 + 2 * i] << 8 | buf[363 + 2 * i]);
	for (i = 0; i < NHARM; i++) {
		v->tab[0][i] = v->w[W_TAB_A + i] > 21 ? v->w[W_TAB_A + i] : 0;
		v->tab[1][i] = v->w[W_TAB_B + i] > 21 ? v->w[W_TAB_B + i] : 0;
	}
	for (int t = 0; t < 2; t++) {
		double e = 0;
		for (i = 0; i < NHARM; i++)
			e += v->tab[t][i] * v->tab[t][i];
		for (i = 0; i < NHARM; i++)
			v->tab[t][i] = e > 0 ? v->tab[t][i] / sqrt(e) : (i == 0);
	}
	if (v->nw > W_NOTES && v->w[W_NOTES] > 1) {
		v->nnotes = v->w[W_NOTES] < 64 ? v->w[W_NOTES] : 64;
		if (v->nnotes > v->nw - W_NOTES - 1)
			v->nnotes = v->nw - W_NOTES - 1;
		for (i = 0; i < (size_t)v->nnotes; i++) {
			uint16_t x = (uint16_t)v->w[W_NOTES + 1 + i];
			v->note_dur[i] = x >> 8;
			v->note_semi[i] = (int8_t)(x & 0xff);
		}
	}

	/* the sampled sound: u32 length, [u32 count, count u32 offsets], 8-bit samples */
	snprintf(path, sizeof(path), "%s/%s.ttvw", dir, id);
	if ((f = fopen(path, "rb"))) {
		unsigned char *w;
		long len;
		fseek(f, 0, SEEK_END);
		len = ftell(f);
		fseek(f, 0, SEEK_SET);
		w = malloc(len > 0 ? len : 1);
		if (w && fread(w, 1, len, f) == (size_t)len && len > 8) {
			uint32_t dl = (uint32_t)w[0] << 24 | w[1] << 16 | w[2] << 8 | w[3];
			long off = 4;
			v->nseg = 1;
			v->seg[0] = 0;
			if ((long)dl + 4 != len) {
				uint32_t c = (uint32_t)w[4] << 24 | w[5] << 16 | w[6] << 8 | w[7];
				if (c >= 2 && c <= 16 && 8 + 4 * (long)c + (long)dl == len) {
					for (i = 0; i < c; i++)
						v->seg[i] = w[8 + 4 * i] << 24 | w[9 + 4 * i] << 16 |
						            w[10 + 4 * i] << 8 | w[11 + 4 * i];
					v->nseg = (int)c - 1;
					off = 8 + 4 * (long)c;
				}
			}
			if (off + (long)dl > len)
				dl = (uint32_t)(len - off);
			v->nwave = (int)dl;
			v->seg[v->nseg] = v->nwave;
			v->wave = malloc(sizeof(float) * (dl ? dl : 1));
			double mean = 0, rms = 0;
			for (i = 0; i < dl; i++)
				mean += w[off + i];
			mean /= dl ? dl : 1;
			for (i = 0; i < dl; i++) {
				v->wave[i] = (float)((w[off + i] - mean) / 128.0);
				rms += v->wave[i] * v->wave[i];
			}
			rms = sqrt(rms / (dl ? dl : 1));
			for (i = 0; i < dl && rms > 0; i++)
				v->wave[i] = (float)(v->wave[i] / rms * 0.707);
		}
		free(w);
		fclose(f);
	}
	return 0;
}

/* ---- text to phonemes --------------------------------------------------- */

struct tok {   /* one phoneme of the input, before timing */
	int ph, stress, word, func, pause_ms, punct;
};

static struct tok toks[MAXSEG];
static int ntok, nword;

static void add_tok(int p, int stress, int func)
{
	if (ntok >= MAXSEG)
		return;
	toks[ntok].ph = p;
	toks[ntok].stress = stress;
	toks[ntok].word = nword;
	toks[ntok].func = func;
	toks[ntok].pause_ms = 0;
	toks[ntok].punct = 0;
	ntok++;
}

static void add_pause(int ms, int punct)
{
	if (ntok > 0 && toks[ntok - 1].ph == 0) {
		if (toks[ntok - 1].pause_ms < ms)
			toks[ntok - 1].pause_ms = ms;
		if (punct)
			toks[ntok - 1].punct = punct;
		return;
	}
	add_tok(0, 0, 0);
	toks[ntok - 1].pause_ms = ms;
	toks[ntok - 1].punct = punct;
}

/* "1EY t" -> phonemes; returns whether any stress was marked */
static int add_phstr(const char *s, int func, int stress_first)
{
	int marked = 0, first = ntok;
	while (*s) {
		int st = 0, p;
		const char *e;
		while (*s == ' ')
			s++;
		if (!*s)
			break;
		if (*s == '1' || *s == '2') {
			st = *s == '1' ? 2 : 1;
			marked = 1;
			s++;
		}
		for (e = s; *e && *e != ' '; e++)
			;
		if ((p = ph_find(s, e - s)) > 0)
			add_tok(p, st, func);
		s = e;
	}
	if (!marked && stress_first && !func) {
		for (int i = first; i < ntok; i++)
			if (ph[toks[i].ph].flags & V) {
				toks[i].stress = 2;
				break;
			}
	}
	return marked;
}

static int isvowel(int c) { return strchr("AEIOU", c) != NULL; }
static int iscons(int c) { return c >= 'A' && c <= 'Z' && !isvowel(c); }

/* does context pattern pat fit word w around position [pos]?  dir -1 is the
 * left context (read right to left), +1 the right one */
static int ctx(const char *w, int pos, const char *pat, int dir)
{
	int n = (int)strlen(pat), i;
	int k = pos;
	for (i = dir < 0 ? n - 1 : 0; dir < 0 ? i >= 0 : i < n; i += dir) {
		char c = pat[i];
		char x = (k >= 0 && w[k]) ? w[k] : ' ';
		if (k < 0)
			x = ' ';
		switch (c) {
		case '#':
			if (!isvowel(x))
				return 0;
			while (k + dir >= 0 && w[k + dir] && isvowel(w[k + dir]))
				k += dir;
			break;
		case ':':
			while (k >= 0 && w[k] && iscons(w[k]))
				k += dir;
			k -= dir;
			break;
		case '^':
			if (!iscons(x))
				return 0;
			break;
		case '.':
			if (!strchr("BDVGJLMNRWZ", x) || x == ' ')
				return 0;
			break;
		case '+':
			if (!strchr("EIY", x) || x == ' ')
				return 0;
			break;
		case '&':
			if (k >= 1 && (w[k] == 'H') && (w[k - 1] == 'C' || w[k - 1] == 'S')) {
				k--;
				break;
			}
			if (!strchr("SCGZXJ", x) || x == ' ')
				return 0;
			break;
		case '@':
			if (k >= 1 && w[k] == 'H' && strchr("TCS", w[k - 1])) {
				k--;
				break;
			}
			if (!strchr("TSRDLZNJ", x) || x == ' ')
				return 0;
			break;
		case '%': {
			static const char *suf[] = { "ING", "ELY", "ER", "ES", "ED", "E", NULL };
			int j, ok = 0;
			for (j = 0; suf[j]; j++) {
				size_t l = strlen(suf[j]);
				if (k >= 0 && !strncmp(w + k, suf[j], l)) {
					k += (int)l - 1;
					ok = 1;
					break;
				}
			}
			if (!ok)
				return 0;
			break;
		}
		case ' ':
			if (x != ' ' && isalpha((unsigned char)x))
				return 0;
			break;
		default:
			if (x != c)
				return 0;
		}
		k += dir;
	}
	return 1;
}

/* English word (upper case, framed by spaces in buf) -> phonemes */
static void nrl_word(const char *word, int func)
{
	char w[128];
	int i, n = (int)strlen(word), start = ntok;
	if (n > 120)
		n = 120;
	w[0] = ' ';
	memcpy(w + 1, word, n);
	w[n + 1] = ' ';
	w[n + 2] = 0;
	i = 1;
	while (i <= n) {
		const struct rule *r;
		int c = w[i];
		if (c >= 'A' && c <= 'Z')
			r = rules[c - 'A'];
		else
			r = rules_punct;
		for (; r->match; r++) {
			size_t ml = strlen(r->match);
			if (strncmp(w + i, r->match, ml))
				continue;
			if (!ctx(w, i - 1, r->left, -1))
				continue;
			if (!ctx(w, i + (int)ml, r->right, 1))
				continue;
			add_phstr(r->out, 1, 0);
			i += (int)ml;
			break;
		}
		if (!r->match)
			i++;
	}
	for (i = start; i < ntok; i++)
		toks[i].func = func;
	if (!func)
		for (i = start; i < ntok; i++)
			if (ph[toks[i].ph].flags & V) {
				toks[i].stress = 2;
				break;
			}
}

static const char *ones[] = { "zero", "one", "two", "three", "four", "five", "six",
	"seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen",
	"fifteen", "sixteen", "seventeen", "eighteen", "nineteen" };
static const char *tens[] = { "", "", "twenty", "thirty", "forty", "fifty", "sixty",
	"seventy", "eighty", "ninety" };

static void say_word(const char *word);

static void say_number(long n)
{
	if (n >= 1000000) {
		say_number(n / 1000000);
		say_word("million");
		n %= 1000000;
		if (!n)
			return;
	}
	if (n >= 1000) {
		say_number(n / 1000);
		say_word("thousand");
		n %= 1000;
		if (!n)
			return;
	}
	if (n >= 100) {
		say_word(ones[n / 100]);
		say_word("hundred");
		n %= 100;
		if (!n)
			return;
	}
	if (n >= 20) {
		say_word(tens[n / 10]);
		if (n % 10)
			say_word(ones[n % 10]);
		return;
	}
	say_word(ones[n]);
}

static void say_word(const char *word)
{
	char lw[128], uw[128];
	size_t i, n = strlen(word);
	int j;
	if (n >= sizeof(lw))
		n = sizeof(lw) - 1;
	for (i = 0; i < n; i++) {
		lw[i] = (char)tolower((unsigned char)word[i]);
		uw[i] = (char)toupper((unsigned char)word[i]);
	}
	lw[n] = uw[n] = 0;
	nword++;
	for (j = 0; lexicon[j].word; j++)
		if (!strcmp(lexicon[j].word, lw)) {
			int func = !strchr(lexicon[j].ph, '1');
			add_phstr(lexicon[j].ph, func, 0);
			return;
		}
	/* a lone letter, or letters with dots (U.S.A.) */
	if (n == 1 && isalpha((unsigned char)uw[0]) && uw[0] != 'A' && uw[0] != 'I') {
		static const char *names[26] = { "1EY", "b 1IY", "s 1IY", "d 1IY", "1IY",
			"1EH f", "J 1IY", "1EY C", "1AY", "J 1EY", "k 1EY", "1EH l", "1EH m",
			"1EH n", "1OW", "p 1IY", "k y 1UW", "1AA r", "1EH s", "t 1IY", "y 1UW",
			"v 1IY", "d 1AH b AX l y UW", "1EH k s", "w 1AY", "z 1IY" };
		add_phstr(names[uw[0] - 'A'], 0, 0);
		return;
	}
	nrl_word(uw, 0);
}

/* Speech Manager embedded command [[...]] */
static int phon_mode;
static double cmd_rate, cmd_pitch;

static void command(const char *c)
{
	char name[8] = { 0 };
	double arg = 0;
	if (sscanf(c, " %4s %lf", name, &arg) < 1)
		return;
	if (!strcasecmp(name, "inpt"))
		phon_mode = strstr(c, "PHON") || strstr(c, "phon");
	else if (!strcasecmp(name, "rate") && arg > 0)
		cmd_rate = arg;
	else if (!strcasecmp(name, "pbas") && arg > 0)
		cmd_pitch = 440.0 * pow(2, (arg - 69) / 12.0);
	else if (!strcasecmp(name, "slnc") && arg > 0)
		add_pause((int)arg, 0);
}

static void apple_phonemes(const char *s, const char *end)
{
	int stress = 0;
	nword++;
	while (s < end) {
		size_t i, best = 0;
		int p = -1;
		if (*s == '1' || *s == '2') {
			stress = *s == '1' ? 2 : 1;
			s++;
			continue;
		}
		if (*s == ' ' || *s == '\t' || *s == '\n') {
			nword++;
			s++;
			continue;
		}
		if (*s == '_' || *s == '%' || *s == '@') {
			add_pause(*s == '_' ? 100 : 60, 0);
			s++;
			continue;
		}
		if (strchr(".!?;:,", *s)) {
			add_pause(*s == ',' ? 220 : 400, *s);
			s++;
			continue;
		}
		for (i = 0; i < sizeof(apple_ph) / sizeof(apple_ph[0]); i++) {
			size_t l = strlen(apple_ph[i].apple);
			if (l > best && (size_t)(end - s) >= l && !strncmp(s, apple_ph[i].apple, l)) {
				best = l;
				p = ph_find(apple_ph[i].ours, strlen(apple_ph[i].ours));
			}
		}
		if (p > 0) {
			add_tok(p, (ph[p].flags & V) ? stress : 0, 0);
			if (ph[p].flags & V)
				stress = 0;
			s += best;
		} else {
			s++; /* = ~ and other marks */
		}
	}
}

static void parse(const char *text)
{
	const char *s = text;
	ntok = nword = 0;
	add_pause(60, 0);
	while (*s) {
		if (s[0] == '[' && s[1] == '[') {
			const char *e = strstr(s, "]]");
			char c[64];
			size_t l;
			if (!e)
				break;
			l = (size_t)(e - s - 2) < sizeof(c) - 1 ? (size_t)(e - s - 2) : sizeof(c) - 1;
			memcpy(c, s + 2, l);
			c[l] = 0;
			command(c);
			s = e + 2;
			continue;
		}
		if (phon_mode) {
			const char *e = strstr(s, "[[");
			if (!e)
				e = s + strlen(s);
			apple_phonemes(s, e);
			s = e;
			continue;
		}
		if (isdigit((unsigned char)*s)) {
			long n = 0;
			while (isdigit((unsigned char)*s))
				n = n * 10 + (*s++ - '0');
			if (*s == ':' && isdigit((unsigned char)s[1])) {
				/* 10:05 */
				long m = 0;
				s++;
				while (isdigit((unsigned char)*s))
					m = m * 10 + (*s++ - '0');
				say_number(n);
				if (m == 0)
					say_word("o'clock");
				else {
					if (m < 10)
						say_word("oh");
					say_number(m);
				}
			} else {
				say_number(n);
			}
			continue;
		}
		if (isalpha((unsigned char)*s)) {
			char w[128];
			size_t n = 0;
			while ((isalpha((unsigned char)*s) || *s == '\'' ||
			        (*s == '.' && isalpha((unsigned char)s[1]) && s[2] == '.')) && n < sizeof(w) - 3) {
				w[n++] = *s++;
				if (s[-1] == '.' && n < sizeof(w) - 1)
					w[n++] = *s++;
			}
			if (*s == '.' && n >= 3 && w[n - 2] == '.')
				w[n++] = *s++;   /* the last dot of a.m. */
			w[n] = 0;
			if (n && w[n - 1] == '\'')
				w[--n] = 0;
			if (!strchr(w, '.') || strlen(w) <= 4) {
				say_word(w);
			} else {
				char *p = w, *q;
				while ((q = strchr(p, '.'))) {
					*q = 0;
					if (*p)
						say_word(p);
					p = q + 1;
				}
			}
			continue;
		}
		switch (*s) {
		case ',': case ';': case ':':
			add_pause(220, ',');
			break;
		case '.': case '!':
			add_pause(420, '.');
			break;
		case '?':
			add_pause(420, '?');
			break;
		case '-':
			if (s[1] == ' ' || s[1] == '-')
				add_pause(150, ',');
			break;
		}
		s++;
	}
	add_pause(120, '.');
}

/* ---- timing and pitch ------------------------------------------------------ */

enum { K_NORM, K_CLOSE, K_BURST, K_ASP };

struct seg {
	int ph, kind, stress, tok;
	double t0, dur;
	double f0;      /* melody pitch for this vowel (0: intonation) */
};

static struct seg segs[MAXSEG * 3];
static int nseg;

static void add_seg(int p, int kind, double dur, int stress, int tok)
{
	struct seg *s;
	if (nseg >= MAXSEG * 3 || dur <= 0)
		return;
	s = &segs[nseg++];
	s->ph = p;
	s->kind = kind;
	s->dur = dur;
	s->stress = stress;
	s->tok = tok;
	s->f0 = 0;
}

static int is_vowelish(int p) { return p > 0 && (ph[p].flags & (V | SON | NAS)); }

static void timing(const struct voice *v, double rate)
{
	double scale = 170.0 / rate;
	int i, note = 0;
	nseg = 0;
	for (i = 0; i < ntok; i++) {
		int p = toks[i].ph, fl = ph[p].flags;
		int next = i + 1 < ntok ? toks[i + 1].ph : 0;
		double d = ph[p].dur;
		/* phrase-final: no vowel between here and the next pause */
		int final = 0, j;
		for (j = i + 1; j < ntok && toks[j].ph; j++)
			if (ph[toks[j].ph].flags & V)
				break;
		if (j >= ntok || toks[j].ph == 0)
			final = toks[j < ntok ? j : ntok - 1].punct != 0;

		if (p == 0) {
			add_seg(0, K_NORM, toks[i].pause_ms * (scale < 1.5 ? scale : 1.5), 0, i);
			continue;
		}
		if (fl & V) {
			if (toks[i].stress == 0)
				d *= toks[i].func ? 0.55 : 0.65;
			else if (toks[i].stress == 1)
				d *= 0.85;
			if (next && (ph[next].flags & (STOP | FRIC | AFFR)) && !(ph[next].flags & VOI))
				d *= 0.85;
			if (final)
				d *= 1.35;
		} else {
			if (i > 0 && toks[i - 1].ph && !(ph[toks[i - 1].ph].flags & V) &&
			    next && !(ph[next].flags & V))
				d *= 0.8;
			if (final)
				d *= 1.15;
		}
		d *= scale;
		if ((fl & V) && v->nnotes) {
			/* a singing voice: each vowel takes the next note of its tune */
			double nd = v->note_dur[note % v->nnotes] * 75.0 * scale;
			add_seg(p, K_NORM, nd > 60 ? nd : 60, toks[i].stress, i);
			segs[nseg - 1].f0 = v->note_semi[note % v->nnotes];
			segs[nseg - 1].f0 = 1000 + segs[nseg - 1].f0; /* semitones, marked */
			note++;
			continue;
		}
		if (fl & STOP) {
			int before_v = next && is_vowelish(next);
			add_seg(p, K_CLOSE, d * 0.75, 0, i);
			add_seg(p, K_BURST, 12, 0, i);
			if (!(fl & VOI) && before_v)
				add_seg(p, K_ASP, 45 * (scale < 1.3 ? scale : 1.3), 0, i);
			continue;
		}
		if (fl & AFFR) {
			add_seg(p, K_CLOSE, d * 0.6, 0, i);
			add_seg(p, K_NORM, d * 0.7, 0, i);
			continue;
		}
		add_seg(p, K_NORM, d, toks[i].stress, i);
	}
	double t = 0;
	for (i = 0; i < nseg; i++) {
		segs[i].t0 = t;
		t += segs[i].dur;
	}
}

/* pitch at time t (ms): a falling line per phrase, a rise on each stressed
 * syllable, a fall (or a rise, for a question) at the end */
struct pitchpt { double t, hz; };
static struct pitchpt *pp;
static int npp;

static void add_pp(double t, double hz)
{
	pp = realloc(pp, sizeof(*pp) * (npp + 1));
	pp[npp].t = t;
	pp[npp].hz = hz;
	npp++;
}

static void intonation(const struct voice *v, double base)
{
	double mod = v->w[W_PMOD] / 100.0;
	int i = 0;
	npp = 0;
	while (i < nseg) {
		int j, k, last_stress = -1, punct;
		double t0, t1, top, bot;
		while (i < nseg && segs[i].ph == 0)
			i++;
		if (i >= nseg)
			break;
		for (j = i; j < nseg && segs[j].ph != 0; j++)
			;
		punct = j < nseg ? toks[segs[j].tok].punct : '.';
		t0 = segs[i].t0;
		t1 = segs[j - 1].t0 + segs[j - 1].dur;
		top = base * (1 + 0.12 * mod);
		bot = base * (1 - 0.08 * mod);
		for (k = i; k < j; k++)
			if ((ph[segs[k].ph].flags & V) && segs[k].stress == 2)
				last_stress = k;
		add_pp(t0, top);
		for (k = i; k < j; k++) {
			struct seg *s = &segs[k];
			double frac = (s->t0 - t0) / (t1 - t0 + 1);
			double line = top + (bot - top) * frac;
			if (s->f0 >= 900) {
				double hz = base * pow(2, (s->f0 - 1000) / 12.0);
				add_pp(s->t0 + 5, hz);
				add_pp(s->t0 + s->dur - 5, hz);
				continue;
			}
			if (!(ph[s->ph].flags & V) || s->stress < 2)
				continue;
			if (k == last_stress && punct != ',') {
				if (punct == '?') {
					add_pp(s->t0, line);
					add_pp(t1, line * (1 + 0.45 * mod));
				} else {
					add_pp(s->t0, line * (1 + 0.22 * mod));
					add_pp(s->t0 + s->dur, line * (1 - 0.18 * mod));
					add_pp(t1, line * (1 - 0.25 * mod));
				}
			} else {
				add_pp(s->t0, line * (1 + 0.14 * mod));
				add_pp(s->t0 + s->dur * 0.5, line * (1 + 0.20 * mod));
				add_pp(s->t0 + s->dur + 30, line);
			}
		}
		if (punct == ',')
			add_pp(t1, bot * (1 + 0.1 * mod));
		else if (last_stress < 0)
			add_pp(t1, bot);
		i = j;
	}
	if (!npp)
		add_pp(0, base);
}

static double pitch_at(double t)
{
	int i;
	if (t <= pp[0].t)
		return pp[0].hz;
	for (i = 1; i < npp; i++)
		if (pp[i].t >= t) {
			double a = pp[i - 1].t, b = pp[i].t;
			if (b - a < 1e-6)
				return pp[i].hz;
			return pp[i - 1].hz + (pp[i].hz - pp[i - 1].hz) * (t - a) / (b - a);
		}
	return pp[npp - 1].hz;
}

/* ---- synthesis ------------------------------------------------------------- */

struct reso { double a, b, c, y1, y2; };

static void reso_set(struct reso *r, double f, double bw)
{
	double rr = exp(-M_PI * bw / FS);
	r->c = -rr * rr;
	r->b = 2 * rr * cos(2 * M_PI * f / FS);
	r->a = 1 - r->b - r->c;
}

static double reso_run(struct reso *r, double x)
{
	double y = r->a * x + r->b * r->y1 + r->c * r->y2;
	r->y2 = r->y1;
	r->y1 = y;
	return y;
}

struct anti { double a, b, c, x1, x2; };

static void anti_set(struct anti *r, double f, double bw)
{
	double rr = exp(-M_PI * bw / FS);
	double c = -rr * rr, b = 2 * rr * cos(2 * M_PI * f / FS), a = 1 - b - c;
	r->a = 1 / a;
	r->b = -b / a;
	r->c = -c / a;
}

static double anti_run(struct anti *r, double x)
{
	double y = r->a * x + r->b * r->x1 + r->c * r->x2;
	r->x2 = r->x1;
	r->x1 = x;
	return y;
}

struct params {
	double f[3], b[3];
	double av, ah, af, fc, fbw, nasal, emph;
};

static double db(double x) { return x <= 0 ? 0 : pow(10, (x - 60) / 20); }

/* the target of segment i at fraction u (0..1) through it */
static void target(int i, double u, struct params *p)
{
	const struct seg *s = &segs[i];
	const struct phon *q = &ph[s->ph];
	int src = s->ph;
	int k;
	if ((s->kind == K_ASP || s->ph == 25 /* h */ || s->ph == 0) && i + 1 < nseg) {
		/* h, aspiration and pauses take the shape of what follows */
		for (k = i + 1; k < nseg && segs[k].ph == segs[i].ph && segs[k].kind != K_NORM; k++)
			;
		if (k < nseg && segs[k].ph)
			src = segs[k].ph;
		else if (i > 0)
			src = segs[i - 1].ph;
	}
	q = &ph[src];
	for (k = 0; k < 3; k++) {
		double a = (&q->f1)[k], e = (&q->e1)[k];
		if ((q->flags & DIPH) && e > 0 && s->ph == src) {
			double w = u < 0.35 ? 0 : (u - 0.35) / 0.65;
			a += (e - a) * w;
		}
		p->f[k] = a;
		p->b[k] = (&q->b1)[k];
	}
	q = &ph[s->ph];
	p->av = p->ah = p->af = 0;
	p->fc = q->fc ? q->fc : 3000;
	p->fbw = q->fbw ? q->fbw : 2000;
	p->nasal = (q->flags & NAS) ? 1 : 0;
	p->emph = s->stress == 2 ? 1 : s->stress == 1 ? 0.5 : 0;
	if (s->ph == 0)
		return;
	switch (s->kind) {
	case K_CLOSE:
		if (q->flags & VOI) {
			p->av = db(40);
			p->f[0] = 180;
		}
		break;
	case K_BURST:
		p->af = db(q->af - 1);
		if (q->flags & VOI)
			p->av = db(38);
		break;
	case K_ASP:
		p->ah = db(52);
		break;
	default:
		if (s->ph == 25)
			p->ah = db(54);
		p->av = db(q->av + (s->stress == 2 ? 2 : 0));
		if (q->flags & (FRIC | AFFR))
			p->af = db(q->af);
	}
}

static void lerp(struct params *a, const struct params *b, double w)
{
	double *x = (double *)a;
	const double *y = (const double *)b;
	size_t i;
	for (i = 0; i < sizeof(*a) / sizeof(double); i++)
		x[i] += (y[i] - x[i]) * w;
}

/* parameters at time t (ms), with transitions into and out of each segment */
static void params_at(double t, int *hint, struct params *p)
{
	int i = *hint;
	struct seg *s;
	double u, tin, tout;
	struct params o;
	while (i + 1 < nseg && t >= segs[i + 1].t0)
		i++;
	*hint = i;
	s = &segs[i];
	u = s->dur > 0 ? (t - s->t0) / s->dur : 0;
	target(i, u, p);
	tin = tout = 35;
	if (s->kind == K_BURST || s->kind == K_CLOSE)
		tin = tout = 8;
	if (tin > s->dur / 2)
		tin = s->dur / 2;
	if (tout > s->dur / 2)
		tout = s->dur / 2;
	if (i > 0 && t - s->t0 < tin) {
		struct params m;
		target(i - 1, 1, &o);
		m = o;
		lerp(&m, p, 0.5);
		double keep[4] = { p->av, p->ah, p->af, p->nasal };
		lerp(&m, p, (t - s->t0) / tin);
		*p = (struct params){ { m.f[0], m.f[1], m.f[2] }, { m.b[0], m.b[1], m.b[2] },
			keep[0], keep[1], keep[2], m.fc, m.fbw, keep[3], m.emph };
		/* amplitudes change quickly */
		if (t - s->t0 < 6) {
			double w = (t - s->t0) / 6;
			p->av = o.av + (p->av - o.av) * (0.5 + 0.5 * w);
		}
	} else if (i + 1 < nseg && s->t0 + s->dur - t < tout) {
		struct params m;
		double keep[4] = { p->av, p->ah, p->af, p->nasal };
		target(i + 1, 0, &o);
		m = o;
		lerp(&m, p, 0.5);
		lerp(&m, p, (s->t0 + s->dur - t) / tout);
		*p = (struct params){ { m.f[0], m.f[1], m.f[2] }, { m.b[0], m.b[1], m.b[2] },
			keep[0], keep[1], keep[2], m.fc, m.fbw, keep[3], m.emph };
		if (s->t0 + s->dur - t < 6) {
			double w = (s->t0 + s->dur - t) / 6;
			p->av = o.av + (p->av - o.av) * (0.5 + 0.5 * w);
		}
	}
}

static float sintab[4096];

static float *synth(const struct voice *v, double base, int *nout)
{
	double total = nseg ? segs[nseg - 1].t0 + segs[nseg - 1].dur : 0;
	int n = (int)(total * FS / 1000) + FS / 10, i, hint = 0, wseg = 0, lastv = -1;
	float *out = calloc(n, sizeof(float));
	struct reso r[5], fr, fr2, np;
	struct anti nz;
	struct params p = { { 0 } };
	double phase[3] = { 0, 0.33, 0.66 }, wpos[3] = { 0, 0, 0 }, lp = 0, lp2 = 0, hp = 0, rad = 0;
	double breath = v->w[W_BREATH] / 100.0, voicing = v->w[W_VOICING] / 192.0;
	double gain = v->w[W_GAIN] > 0 ? v->w[W_GAIN] / 100.0 : 1;
	double bpct[3], df[3];
	double chorus = v->w[W_CHORUS] ? 1 + v->w[W_CHORUS] / 100.0 : 0;
	int wavemode = v->wave ? v->w[W_WAVEMODE] : 0;
	double f0 = base, jit = 0;

	if (!out)
		return NULL;
	for (i = 0; i < 4096; i++)
		sintab[i] = (float)sin(2 * M_PI * i / 4096);
	for (i = 0; i < 3; i++) {
		bpct[i] = v->w[W_B1PCT + i] > 0 ? v->w[W_B1PCT + i] / 100.0 : 1;
		df[i] = v->w[W_DF1 + i];
	}
	memset(r, 0, sizeof(r));
	memset(&fr, 0, sizeof(fr));
	memset(&fr2, 0, sizeof(fr2));
	memset(&np, 0, sizeof(np));
	memset(&nz, 0, sizeof(nz));
	reso_set(&r[3], v->w[W_F4] > 0 ? v->w[W_F4] : 3300, v->w[W_B4] > 0 ? v->w[W_B4] : 250);
	reso_set(&r[4], v->w[W_F5] > 0 && v->w[W_F5] < FS / 2 - 500 ? v->w[W_F5] : 3750,
	         v->w[W_B5] > 0 ? v->w[W_B5] : 200);

	for (i = 0; i < n; i++) {
		double t = i * 1000.0 / FS, x = 0, noise = frand(), asp, fric;
		int k;
		if (i % BLOCK == 0) {
			double fn;
			params_at(t, &hint, &p);
			for (k = 0; k < 3; k++) {
				double f = p.f[k] + df[k], bw = p.b[k] * bpct[k];
				if (f < 150 + 100 * k)
					f = 150 + 100 * k;
				reso_set(&r[k], f, bw);
			}
			fn = v->w[W_FNP] > 0 ? v->w[W_FNP] : 270;
			reso_set(&np, fn, 100);
			anti_set(&nz, p.nasal ? (v->w[W_FNZ] > 0 ? v->w[W_FNZ] + 80 : 450) : fn, 100);
			reso_set(&fr, p.fc, p.fbw);
			reso_set(&fr2, p.fc * 1.5 < FS / 2 - 1000 ? p.fc * 1.5 : FS / 2 - 1000, p.fbw);
			f0 = pitch_at(t);
			if (cmd_pitch > 0 && v->nnotes == 0)
				f0 *= 1; /* already in base */
			jit = 1 + 0.004 * frand();
			/* one-shot sampled sounds restart on each new vowel */
			if (wavemode == 2 && segs[hint].ph && (ph[segs[hint].ph].flags & V) && hint != lastv) {
				lastv = hint;
				wseg = (wseg + 1) % v->nseg;
				wpos[0] = wpos[1] = wpos[2] = v->seg[wseg];
			}
		}

		/* the voice source */
		if (p.av > 0) {
			int c, nc = chorus > 0 ? 2 : 1;
			for (c = 0; c < nc; c++) {
				double fc = f0 * jit * (c ? chorus : 1), s = 0;
				if (wavemode) {
					/* the voice's sampled sound, played at the speaking pitch */
					double step = fc / base;
					int lo = wavemode == 2 ? v->seg[wseg] : 0;
					int hi = wavemode == 2 ? v->seg[wseg + 1] : v->nwave;
					int a;
					if (hi - lo < 4) {
						lo = 0;
						hi = v->nwave;
					}
					if (wpos[c] >= hi || wpos[c] < lo)
						wpos[c] = lo + fmod(wpos[c] - lo, hi - lo);
					a = (int)wpos[c];
					double fr_ = wpos[c] - a;
					s = v->wave[a] * (1 - fr_) + v->wave[a + 1 < hi ? a + 1 : lo] * fr_;
					wpos[c] += step * 11127.0 / FS;
				} else {
					int h;
					double e = p.emph;
					for (h = 0; h < NHARM && (h + 1) * fc < FS * 0.45; h++) {
						double amp = v->tab[0][h] * (1 - e) + v->tab[1][h] * e;
						if (amp > 0)
							s += amp * sintab[(int)(phase[c] * (h + 1) * 4096) & 4095];
					}
					phase[c] += fc / FS;
					if (phase[c] >= 1)
						phase[c] -= 1;
				}
				x += s * (c ? 0.8 : 1);
			}
			x *= p.av * voicing * 3;
		}
		/* breath and aspiration go through the vocal tract */
		lp = lp * 0.5 + noise * 0.5;
		asp = lp * (p.ah * 1.5 + p.av * breath * 0.6);
		x += asp;
		/* the vocal tract: nasal pole and zero, five formants */
		x = anti_run(&nz, reso_run(&np, x));
		for (k = 0; k < 5; k++)
			x = reso_run(&r[k], x);
		/* radiation from the lips: +6 dB per octave */
		{
			double y = x - rad;
			rad = x;
			x = y * 4;
		}
		/* frication: noise through its own band */
		fric = 0;
		if (p.af > 0) {
			double nn = noise;
			hp = nn - lp2;
			lp2 = nn;
			if (p.fc < 3500)
				hp = nn;   /* sh, k, p: no extra tilt */
			fric = (reso_run(&fr, hp) * 0.7 + reso_run(&fr2, hp) * 0.15) * p.af * 1.2;
			if (p.fbw >= 4000)
				fric = hp * p.af * 0.3;  /* f and th: a flat hiss */
		}
		out[i] = (float)((x + fric) * gain);
	}
	*nout = n;
	return out;
}

/* ---- output ---------------------------------------------------------------- */

static void put16(FILE *f, int v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void put32(FILE *f, uint32_t v) { put16(f, v & 0xffff); put16(f, v >> 16); }

static int write_wav(FILE *f, const float *x, int n)
{
	double peak = 1e-9;
	int i;
	for (i = 0; i < n; i++)
		if (fabs(x[i]) > peak)
			peak = fabs(x[i]);
	fwrite("RIFF", 1, 4, f);
	put32(f, 36 + 2 * (uint32_t)n);
	fwrite("WAVEfmt ", 1, 8, f);
	put32(f, 16);
	put16(f, 1);
	put16(f, 1);
	put32(f, FS);
	put32(f, FS * 2);
	put16(f, 2);
	put16(f, 16);
	fwrite("data", 1, 4, f);
	put32(f, 2 * (uint32_t)n);
	for (i = 0; i < n; i++)
		put16(f, (int)lrint(x[i] / peak * 29000));
	return ferror(f) ? -1 : 0;
}

static const char *default_dir(void)
{
	static const char *dirs[] = { "/usr/local/share/macintalk", "/usr/share/macintalk",
		"sources/macintalk", NULL };
	const char *e = getenv("MACINTALK_VOICES");
	int i;
	if (e && *e)
		return e;
	for (i = 0; dirs[i]; i++) {
		DIR *d = opendir(dirs[i]);
		if (d) {
			closedir(d);
			return dirs[i];
		}
	}
	return dirs[0];
}

static void usage(void)
{
	fprintf(stderr,
		"usage: macintalk [-v voice] [-d voicedir] [-o out.wav] [-r wpm] [-p hz] text...\n"
		"       macintalk -l [-d voicedir]\n"
		"       macintalk -x [-v voice] text...   (print phonemes)\n");
	exit(2);
}

int main(int argc, char **argv)
{
	const char *dir = NULL, *vname = "fred", *outname = NULL;
	char text[8192] = "", id[64];
	double rate = 0, pitch = 0;
	int list = 0, show = 0, i, n;
	struct voice v;
	float *pcm;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v") && i + 1 < argc)
			vname = argv[++i];
		else if (!strcmp(argv[i], "-d") && i + 1 < argc)
			dir = argv[++i];
		else if (!strcmp(argv[i], "-o") && i + 1 < argc)
			outname = argv[++i];
		else if (!strcmp(argv[i], "-r") && i + 1 < argc)
			rate = atof(argv[++i]);
		else if (!strcmp(argv[i], "-p") && i + 1 < argc)
			pitch = atof(argv[++i]);
		else if (!strcmp(argv[i], "-l"))
			list = 1;
		else if (!strcmp(argv[i], "-x"))
			show = 1;
		else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
			usage();
		else if (argv[i][0] == '-' && argv[i][1])
			usage();
		else {
			if (*text)
				strncat(text, " ", sizeof(text) - strlen(text) - 1);
			strncat(text, argv[i], sizeof(text) - strlen(text) - 1);
		}
	}
	if (!dir)
		dir = default_dir();
	if (list) {
		DIR *d = opendir(dir);
		struct dirent *e;
		if (!d) {
			fprintf(stderr, "macintalk: %s: %s\n", dir, strerror(errno));
			return 1;
		}
		while ((e = readdir(d))) {
			size_t l = strlen(e->d_name);
			if (l > 5 && !strcmp(e->d_name + l - 5, ".ttvd") && l - 5 < sizeof(id)) {
				memcpy(id, e->d_name, l - 5);
				id[l - 5] = 0;
				if (!voice_load(&v, dir, id))
					printf("%-12s %-12s %3d Hz %3d wpm  %s\n", id, v.name, v.w[W_PITCH],
					       v.w[W_RATE], v.comment);
			}
		}
		closedir(d);
		return 0;
	}
	if (!*text)
		usage();
	for (i = 0; vname[i] && i < (int)sizeof(id) - 1; i++)
		id[i] = vname[i] == ' ' ? '-' : (char)tolower((unsigned char)vname[i]);
	id[i] = 0;
	if (voice_load(&v, dir, id)) {
		fprintf(stderr, "macintalk: no voice %s in %s (run tools/fetch-macintalk.py)\n",
		        vname, dir);
		return 1;
	}
	parse(text);
	if (show) {
		for (i = 0; i < ntok; i++) {
			if (toks[i].ph == 0) {
				printf("_ ");
				continue;
			}
			printf("%s%s ", toks[i].stress == 2 ? "1" : toks[i].stress == 1 ? "2" : "",
			       ph[toks[i].ph].name);
		}
		printf("\n");
		return 0;
	}
	if (!rate)
		rate = cmd_rate > 0 ? cmd_rate : v.w[W_RATE] > 0 ? v.w[W_RATE] : 170;
	if (!pitch)
		pitch = cmd_pitch > 0 ? cmd_pitch : v.w[W_PITCH] > 0 ? v.w[W_PITCH] : 100;
	timing(&v, rate);
	intonation(&v, pitch);
	if (getenv("MACINTALK_DEBUG"))
		for (i = 0; i < nseg; i++)
			fprintf(stderr, "%7.1f %6.1f %-3s k%d s%d\n", segs[i].t0, segs[i].dur,
			        ph[segs[i].ph].name, segs[i].kind, segs[i].stress);
	pcm = synth(&v, pitch, &n);
	if (!pcm)
		return 1;
	if (outname) {
		FILE *f = fopen(outname, "wb");
		if (!f || write_wav(f, pcm, n) || fclose(f)) {
			fprintf(stderr, "macintalk: %s: %s\n", outname, strerror(errno));
			return 1;
		}
	} else if (write_wav(stdout, pcm, n) || fflush(stdout)) {
		return 1;
	}
	return 0;
}
