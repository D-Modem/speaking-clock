/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The PlainTalk (MacinTalk Pro) voices: Victoria, Bruce and Agnes.
 *
 * Included by macintalk.c.  It reads only the voice's recorded-sound data
 * (the 'gtss' 3 resource, saved as <voice>.gtss by tools/fetch-macintalk.py)
 * and joins the recorded half-diphones itself, with the pitch and timing
 * that macintalk.c works out.  docs/plaintalk-format.md describes the data.
 * None of Apple's code is used.
 */

#define PT_NW 50            /* wave-phonemes: the diphone tables are 50 x 50 */
#define PT_MAXN 256         /* most samples in a coded period */

struct ptunit {             /* one coded half-diphone */
	uint32_t off, end;      /* in the file */
	int per0, nper;         /* its period lengths (voiced streams only) */
};

struct ptvoice {
	unsigned char *d;
	size_t n;
	uint32_t sec[13];
	int nom;                /* the voice's own period, samples at 22050 Hz */
	int dim;                /* samples per codebook vector */
	int N;                  /* samples in a coded period */
	int16_t cb[256][4];
	uint32_t tab[2][PT_NW * PT_NW][2];
	struct ptunit *unit[4]; /* streams 2..5 */
	int nunit[4];
	uint16_t *per[2];       /* period lengths for streams 2 and 3 */
	int nper[2];
};

static uint32_t be32(const unsigned char *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static int be16(const unsigned char *p) { return p[0] << 8 | p[1]; }

static int u32cmp(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return x < y ? -1 : x > y;
}

/* How many of the units parse cleanly as frames of N samples: whole
 * frames, then a last frame whose first byte has the top bit set and holds
 * its index count, then one byte and up to 3 bytes of padding. */
static int pt_fit(struct ptvoice *v, int N)
{
	int s, i, ok = 0, F = 2 + N / v->dim;
	for (s = 0; s < 4; s++)
		for (i = 0; i < v->nunit[s]; i++) {
			uint32_t p = v->unit[s][i].off + 1, e = v->unit[s][i].end;
			while (p < e && v->d[p] < 0x80)
				p += F;
			if (p < e) {
				uint32_t end = p + 2 + (v->d[p] & 0x7f) + 1;
				ok += end <= e && e <= end + 3;
			}
		}
	return ok;
}

static int pt_load(struct ptvoice *v, const char *dir, const char *id)
{
	char path[1024];
	FILE *f;
	long len;
	int i, s, t, best = -1;

	memset(v, 0, sizeof(*v));
	snprintf(path, sizeof(path), "%s/%s.gtss", dir, id);
	if (!(f = fopen(path, "rb")))
		return -1;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	v->d = malloc(len > 0 ? len : 1);
	if (!v->d || len < 64 || fread(v->d, 1, len, f) != (size_t)len) {
		fclose(f);
		goto bad;
	}
	fclose(f);
	v->n = len;
	if (be16(v->d) != PT_NW)
		goto bad;
	v->nom = be16(v->d + 2);
	for (i = 0; i < 12; i++)
		v->sec[i] = be32(v->d + 16 + 4 * i);
	v->sec[12] = len;
	for (i = 0; i < 12; i++)
		if (v->sec[i] > v->sec[i + 1])
			goto bad;
	if (v->nom < 40 || v->nom > 400)
		goto bad;
	/* section 10: the codebook, 256 vectors */
	v->dim = (int)(v->sec[11] - v->sec[10]) / 512;
	if (v->dim != 4)   /* the high quality voices code differently; not done yet */
		goto bad;
	for (i = 0; i < 256 * v->dim; i++)
		v->cb[i / v->dim][i % v->dim] = (int16_t)be16(v->d + v->sec[10] + 2 * i);
	/* sections 0 and 1: where the two halves of each diphone are */
	for (t = 0; t < 2; t++) {
		if (v->sec[t + 1] - v->sec[t] < PT_NW * PT_NW * 8)
			goto bad;
		for (i = 0; i < PT_NW * PT_NW; i++) {
			v->tab[t][i][0] = be32(v->d + v->sec[t] + 8 * i);
			v->tab[t][i][1] = be32(v->d + v->sec[t] + 8 * i + 4);
		}
	}
	/* sections 6 and 7: the period lengths of each unit of streams 2 and 3 */
	for (t = 0; t < 2; t++) {
		const unsigned char *p = v->d + v->sec[6 + t];
		int n = (int)(v->sec[7 + t] - v->sec[6 + t]) / 2;
		v->per[t] = malloc(sizeof(uint16_t) * (n ? n : 1));
		for (i = 0; i < n; i++)
			v->per[t][i] = (uint16_t)be16(p + 2 * i);
		v->nper[t] = n;
	}
	/* The units of each stream in order.  Streams 2 and 4 hold the first
	 * halves (table 0), 3 and 5 the second halves (table 1); 4 and 5 the
	 * halves that are consonants, wave-phoneme 31 and up. */
	for (s = 0; s < 4; s++) {
		int side = s & 1, cons = s >= 2, n = 0;
		uint32_t *o = malloc(sizeof(uint32_t) * (PT_NW * PT_NW + 1));
		for (i = 0; i < PT_NW * PT_NW; i++) {
			int ph = side ? i % PT_NW : i / PT_NW;
			uint32_t a = v->tab[side][i][0];
			if (a >= 0xffff || (ph >= 31) != cons)
				continue;
			if (v->sec[2 + s] + 4 * a < v->sec[3 + s])
				o[n++] = a * 4;
		}
		qsort(o, n, sizeof(*o), u32cmp);
		v->unit[s] = malloc(sizeof(struct ptunit) * (n + 1));
		v->nunit[s] = 0;
		for (i = 0; i < n; i++) {
			struct ptunit *u;
			if (i && o[i] == o[i - 1])
				continue;
			u = &v->unit[s][v->nunit[s]++];
			u->off = v->sec[2 + s] + o[i];
			u->per0 = u->nper = 0;
		}
		for (i = 0; i < v->nunit[s]; i++)
			v->unit[s][i].end = i + 1 < v->nunit[s] ? v->unit[s][i + 1].off : v->sec[3 + s];
		free(o);
		if (!cons) {   /* one period list per unit, each ended by 0xffff */
			int k = 0;
			for (i = 0; i < v->nunit[s]; i++) {
				v->unit[s][i].per0 = k;
				while (k < v->nper[side] && v->per[side][k] != 0xffff)
					k++;
				v->unit[s][i].nper = k - v->unit[s][i].per0;
				k++;
			}
		}
	}
	/* the coded period length isn't in the header: find the one that fits */
	for (i = 8; i <= PT_MAXN; i += 4) {
		int fit = pt_fit(v, i);
		if (fit > best) {
			best = fit;
			v->N = i;
		}
	}
	return 0;
bad:
	fprintf(stderr, "macintalk: %s: not a PlainTalk voice this program can read\n", path);
	free(v->d);
	v->d = NULL;
	return -1;
}

static struct ptunit *pt_find_unit(struct ptvoice *v, int s, uint32_t off)
{
	int lo = 0, hi = v->nunit[s] - 1;
	while (lo <= hi) {
		int m = (lo + hi) / 2;
		if (v->unit[s][m].off == off)
			return &v->unit[s][m];
		if (v->unit[s][m].off < off)
			lo = m + 1;
		else
			hi = m - 1;
	}
	return NULL;
}

/* the table entry for the diphone l-r, following the table's redirections */
static int pt_entry(struct ptvoice *v, int side, int l, int r, uint32_t *off)
{
	int k;
	for (k = 0; k < 6; k++) {
		uint32_t a, b;
		if (l < 0 || r < 0 || l >= PT_NW || r >= PT_NW)
			return -1;
		a = v->tab[side][l * PT_NW + r][0];
		b = v->tab[side][l * PT_NW + r][1];
		if (a == 0xffffffff)
			return -1;
		if (a != 0xffff) {
			*off = a * 4;
			return 0;
		}
		l = b >> 16;
		r = b & 0xffff;
	}
	return -1;
}

/* one decoded period (or, in the consonant streams, one frame) */
struct ptframe {
	float x[PT_MAXN];
	int len;        /* its length as recorded, samples */
	int voiced;     /* follows the pitch */
};

static struct ptframe *frames;
static int nframes, aframes;

static struct ptframe *pt_new_frame(void)
{
	if (nframes >= aframes) {
		aframes = aframes ? aframes * 2 : 1024;
		frames = realloc(frames, sizeof(*frames) * aframes);
		if (!frames) {
			fprintf(stderr, "macintalk: out of memory\n");
			exit(1);
		}
	}
	return &frames[nframes++];
}

/* Decode the half-diphone at byte off of its stream.  A unit is one byte,
 * then frames of: a gain byte (4-bit exponent, 4-bit mantissa), a lag byte
 * and N/dim codebook indices.  Each period is the signal one period plus
 * (lag - 1) samples back, plus the gain times the codebook vectors.  A gain
 * byte with the top bit set starts the last, partial frame, which we leave
 * out. */
static int pt_unit(struct ptvoice *v, int side, int cons, uint32_t off, int voiced)
{
	int s = side + 2 * cons, k = 0, i, N = v->N, nidx = v->N / v->dim, f0 = nframes;
	struct ptunit *u = pt_find_unit(v, s, v->sec[2 + s] + off);
	const unsigned char *p, *e;

	if (!u)
		return -1;
	p = v->d + u->off + 1;
	e = v->d + u->end;
	while (p + 2 + nidx <= e && !(p[0] & 0x80)) {
		int A = p[0], lag = N + p[1] - 1;
		double g = (16 + (A & 15)) * pow(2, A >> 4) / 256.0 / 32768.0;
		int len = cons ? v->nom : k < u->nper ? v->per[side][u->per0 + k] : v->nom;
		struct ptframe *fr = pt_new_frame();
		for (i = 0; i < N; i++) {
			/* sample i of this frame is sample k*N + i of the unit */
			int back = k * N + i - lag;
			double x = 0;
			if (k > 0 && back >= 0) {
				int bf = back / N, bi = back % N;
				x = bf < k ? frames[f0 + bf].x[bi] : fr->x[bi];
			}
			fr->x[i] = (float)x;
		}
		for (i = 0; i < nidx; i++) {
			const int16_t *c = v->cb[p[2 + i]];
			int j;
			for (j = 0; j < v->dim; j++)
				fr->x[i * v->dim + j] += (float)(c[j] * g);
		}
		if (len < 20 || len > 400)
			len = v->nom;
		fr->len = len;
		fr->voiced = voiced;
		p += 2 + nidx;
		k++;
	}
	return k;
}

/* ---- from our phonemes to the voice's wave-phonemes ---- */

static int pt_wave(int p, int next, int prev)
{
	static const struct { const char *ph; int w; } map[] = {
		{ "IY", 2 }, { "IH", 3 }, { "IX", 4 }, { "EH", 5 }, { "AE", 6 }, { "AH", 7 },
		{ "AX", 8 }, { "AA", 9 }, { "AO", 10 }, { "UH", 11 }, { "UW", 12 }, { "OW", 13 },
		{ "ER", 14 }, { "AY", 19 }, { "EY", 20 }, { "OY", 21 }, { "AW", 22 },
		{ "l", 24 }, { "m", 25 }, { "n", 26 }, { "N", 27 }, { "y", 28 }, { "r", 29 },
		{ "w", 30 }, { "b", 31 }, { "d", 32 }, { "g", 33 }, { "v", 34 }, { "D", 35 },
		{ "z", 36 }, { "Z", 37 }, { "f", 38 }, { "T", 39 }, { "s", 40 }, { "S", 41 },
		{ "h", 42 }, { "p", 43 }, { "t", 45 }, { "k", 48 },
	};
	int i, w = -1;
	if (p <= 0)
		return 0;
	for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
		if (!strcmp(ph[p].name, map[i].ph))
			w = map[i].w;
	if (w < 0)
		return 0;
	/* vowels before r take their r-coloured form */
	if (next > 0 && !strcmp(ph[next].name, "r")) {
		if (w == 2 || w == 3)
			w = 15;
		else if (w == 9)
			w = 16;
		else if (w == 10 || w == 13)
			w = 17;
		else if (w == 11 || w == 12)
			w = 18;
	}
	/* p, t, k after s are unaspirated */
	if ((w == 43 || w == 45 || w == 48) && prev > 0 && !strcmp(ph[prev].name, "s"))
		w++;
	return w;
}

/* a close relative to use when a diphone is missing */
static int pt_similar(int w)
{
	switch (w) {
	case 44: return 43;
	case 46: return 45;
	case 49: return 48;
	case 15: return 3;
	case 16: return 9;
	case 17: return 13;
	case 18: return 12;
	case 4: return 3;
	case 7: return 8;
	case 27: return 26;
	case 24: return 23;
	case 34: return 38;
	case 36: return 40;
	case 37: return 41;
	default: return -1;
	}
}

static int pt_half(struct ptvoice *v, int side, int l, int r, int voiced)
{
	uint32_t off;
	int tries;
	for (tries = 0; tries < 4; tries++) {
		int ll = l, rr = r;
		if ((tries & 1) && pt_similar(l) >= 0)
			ll = pt_similar(l);
		if ((tries & 2) && pt_similar(r) >= 0)
			rr = pt_similar(r);
		if (tries && ll == l && rr == r)
			continue;
		if (pt_entry(v, side, ll, rr, &off))
			continue;
		return pt_unit(v, side, (side ? rr : ll) >= 31, off, voiced);
	}
	if (getenv("MACINTALK_DEBUG"))
		fprintf(stderr, "no diphone %d-%d (half %d)\n", l, r, side);
	return -1;
}

static float *pt_synth(struct ptvoice *v, int *nout)
{
	double total = nseg ? segs[nseg - 1].t0 + segs[nseg - 1].dur : 0;
	int cap = (int)(total * 2.6 * FS / 1000) + FS, n = 0, i;
	float *out = calloc(cap, sizeof(float));
	int *w = malloc(sizeof(int) * (ntok + 2));
	double t_ms = 0;

	if (!out || !w)
		return NULL;
	for (i = 0; i < ntok; i++)
		w[i] = pt_wave(toks[i].ph, i + 1 < ntok ? toks[i + 1].ph : 0, i ? toks[i - 1].ph : 0);

	for (i = 0; i < ntok; i++) {
		double want = 0, have = 0, pos, step;
		int k, f0, f1, l = i ? w[i - 1] : 0, r = i + 1 < ntok ? w[i + 1] : 0, voiced;
		for (k = 0; k < nseg; k++)
			if (segs[k].tok == i)
				want += segs[k].dur;
		if (w[i] == 0) {   /* a pause */
			int m = (int)(want * FS / 1000);
			for (k = 0; k < m && n < cap; k++)
				out[n++] = 0;
			t_ms += want;
			continue;
		}
		/* this phoneme is the second half of l-it and the first half of it-r */
		voiced = w[i] < 38;
		f0 = nframes;
		pt_half(v, 1, l, w[i], voiced);
		pt_half(v, 0, w[i], r, voiced);
		f1 = nframes;
		if (f1 == f0)
			continue;
		for (k = f0; k < f1; k++)
			have += frames[k].len;
		have = have * 1000.0 / FS;
		/* stretch or squeeze it to its length by repeating or dropping frames */
		step = want > 0 ? have / want : 1;
		if (step < 0.4)
			step = 0.4;
		if (step > 2.5)
			step = 2.5;
		pos = 0;
		while (pos < have) {
			int fi, j, L;
			double acc = 0, per;
			struct ptframe *fr;
			for (fi = f0; fi < f1 - 1; fi++) {
				acc += frames[fi].len * 1000.0 / FS;
				if (acc > pos)
					break;
			}
			fr = &frames[fi];
			/* a voiced period is resampled to the pitch wanted */
			per = fr->voiced ? FS / pitch_at(t_ms) : fr->len;
			L = (int)lrint(per < 20 ? 20 : per);
			if (n + L >= cap)
				break;
			for (j = 0; j < L; j++) {
				double t = (double)j * v->N / L, fr_;
				int a = (int)t;
				fr_ = t - a;
				out[n + j] = fr->x[a % v->N] * (float)(1 - fr_) +
				             fr->x[(a + 1) % v->N] * (float)fr_;
			}
			n += L;
			t_ms += L * 1000.0 / FS;
			pos += L * 1000.0 / FS * step;
		}
		nframes = f0;
	}
	free(w);
	*nout = n;
	return out;
}
