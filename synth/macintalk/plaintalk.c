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
#define PT_LOWPASS 6000.0   /* Hz */

struct ptunit {             /* one coded half-diphone */
	uint32_t off, end;      /* in the file */
	int per0, nper;         /* its period lengths (voiced streams only) */
};

struct ptvoice {
	unsigned char *d;
	size_t n;
	uint32_t sec[13];
	int nom;                /* the speaker's usual period, samples at 22050 Hz */
	int dim;                /* samples per codebook vector: 4, or 2 in high quality */
	int align;              /* units start on multiples of this many bytes */
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
 * its index count, then one byte and padding to the next unit. */
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
				ok += end <= e && e < end + v->align;
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
	if (v->dim != 4 && v->dim != 2)
		goto bad;
	/* the tables count in units of 4 bytes, or 8 in the high quality voices */
	v->align = 16 / v->dim;
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
	/* the speaker's usual period: the median of the recorded ones (the
	 * header's value is off for some voices) */
	{
		static int hist[401];
		int total = 0, acc = 0;
		memset(hist, 0, sizeof(hist));
		for (t = 0; t < 2; t++)
			for (i = 0; i < v->nper[t]; i++)
				if (v->per[t][i] >= 20 && v->per[t][i] <= 400) {
					hist[v->per[t][i]]++;
					total++;
				}
		for (i = 20; i <= 400 && total; i++) {
			acc += hist[i];
			if (2 * acc >= total) {
				v->nom = i;
				break;
			}
		}
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
			if (a == 0xffff || a == 0xffffffff || (ph >= 31) != cons)
				continue;
			if (v->sec[2 + s] + (uint32_t)v->align * a < v->sec[3 + s])
				o[n++] = a * v->align;
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
	if (getenv("MACINTALK_DEBUG"))
		for (s = 0; s < 2; s++) {
			int lists = 0;
			for (i = 0; i < v->nper[s]; i++)
				lists += v->per[s][i] == 0xffff;
			fprintf(stderr, "%s: stream %d: %d units, %d period lists\n", id, 2 + s,
			        v->nunit[s], lists);
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
			*off = a * v->align;
			return 0;
		}
		l = b >> 16;
		r = b & 0xffff;
	}
	return -1;
}

/* The decoded sound of the phoneme being made, and its pitch marks */
struct ptmark {
	int pos, per;   /* where a period starts in sig, and its length */
	int lo, hi;     /* the half-diphone it belongs to, in sig */
	int voiced;
};

static float *sig;
static int nsig, asig;
static struct ptmark *marks;
static int nmarks, amarks;

static void *grow(void *p, int *cap, int need, size_t size)
{
	if (need <= *cap)
		return p;
	while (*cap < need)
		*cap = *cap ? *cap * 2 : 4096;
	p = realloc(p, size * *cap);
	if (!p) {
		fprintf(stderr, "macintalk: out of memory\n");
		exit(1);
	}
	return p;
}

static void pt_mark(int pos, int per, int lo, int voiced)
{
	marks = grow(marks, &amarks, nmarks + 1, sizeof(*marks));
	marks[nmarks].pos = pos;
	marks[nmarks].per = per;
	marks[nmarks].lo = lo;
	marks[nmarks].hi = 0;
	marks[nmarks].voiced = voiced;
	nmarks++;
}

/* Decode the half-diphone at byte off of its stream and add it to sig.
 * A unit is one byte, then frames of: a gain byte (4-bit exponent, 4-bit
 * mantissa), a lag byte and N/dim codebook indices, N samples of sound.
 * Each sample is the one a pitch period back (N + lag - 1 samples) plus
 * the gain times its codebook vector's sample.  A gain byte with the top
 * bit set starts the last frame, which holds its low 7 bits of indices. */
static int pt_unit(struct ptvoice *v, int side, int cons, uint32_t off, int voiced)
{
	int s = side + 2 * cons, k = 0, i, N = v->N, start = nsig, m0 = nmarks;
	struct ptunit *u = pt_find_unit(v, s, v->sec[2 + s] + off);
	const unsigned char *p, *e;
	int lags[1024], nl = 0;

	if (!u)
		return -1;
	p = v->d + u->off + 1;
	e = v->d + u->end;
	while (p + 2 <= e) {
		int A = p[0], B = p[1], last = A & 0x80, nidx = last ? A & 0x7f : N / v->dim;
		int lag = N + B - 1;
		double g;
		if (last)   /* its gain byte comes after its indices */
			A = p + 2 + nidx < e ? p[2 + nidx] : 0x3f;
		g = (16 + (A & 15)) * pow(2, A >> 4) / 256.0 / 32768.0;
		if (p + 2 + nidx > e)
			break;
		sig = grow(sig, &asig, nsig + nidx * v->dim, sizeof(*sig));
		for (i = 0; i < nidx * v->dim; i++) {
			int back = nsig - lag;
			double x = k > 0 && back >= start ? sig[back] : 0;
			sig[nsig++] = (float)(x + v->cb[p[2 + i / v->dim]][i % v->dim] * g);
		}
		if (nl < 1024)
			lags[nl++] = B < 50 ? lag : 0;
		p += 2 + nidx;
		k++;
		if (last)
			break;
	}
	/* pitch marks: the recorded period lengths, or for the consonants
	 * the lags (unvoiced: the voice's usual period) */
	if (!cons) {
		int pos = start;
		for (i = 0; i < u->nper && pos < nsig; i++) {
			int per = v->per[side][u->per0 + i];
			if (per < 20 || per > 400)
				per = v->nom;
			pt_mark(pos, per, start, 1);
			pos += per;
		}
	} else {
		int pos = start;
		while (pos < nsig) {
			int f = (pos - start) / N, per = f < nl ? lags[f] : 0, vo = voiced && per > 0;
			if (!vo || per < 20 || per > 400)
				per = v->nom;
			pt_mark(pos, per, start, vo);
			pos += per;
		}
	}
	for (i = m0; i < nmarks; i++)
		marks[i].hi = nsig;
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

/* The coding (2 bits a sample) leaves a hiss over the whole band: take
 * off what is above the speech (a 4-pole low-pass at PT_LOWPASS Hz). */
static void pt_lowpass(float *x, int n)
{
	int pass, i;
	double w0 = 2 * M_PI * PT_LOWPASS / FS, al = sin(w0) / (2 * 0.7071), c = cos(w0);
	double b0 = (1 - c) / 2 / (1 + al), b1 = (1 - c) / (1 + al), a1 = -2 * c / (1 + al),
	       a2 = (1 - al) / (1 + al);
	for (pass = 0; pass < 2; pass++) {
		double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
		for (i = 0; i < n; i++) {
			double y = b0 * x[i] + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
			x2 = x1;
			x1 = x[i];
			y2 = y1;
			y1 = y;
			x[i] = (float)y;
		}
	}
}

static float *pt_synth(struct ptvoice *v, int *nout)
{
	double total = nseg ? segs[nseg - 1].t0 + segs[nseg - 1].dur : 0;
	int cap = (int)(total * 2.6 * FS / 1000) + 2 * FS, i, j;
	float *out = calloc(cap, sizeof(float));
	int *w = malloc(sizeof(int) * (2 * ntok + 2)), nw = 0;
	double *wants = malloc(sizeof(double) * (2 * ntok + 2));
	double cur = 0;   /* output time of the next pitch mark, samples */

	if (!out || !w || !wants) {
		free(out);
		free(w);
		free(wants);
		return NULL;
	}
	/* each phoneme becomes one wave-phoneme, except ch and j, which the
	 * voice makes of t and sh, d and zh: the stop takes the phoneme's
	 * first (closure) segment and the hiss the rest */
	for (i = 0; i < ntok; i++) {
		const char *name = toks[i].ph > 0 ? ph[toks[i].ph].name : "_";
		double want = 0, first = -1;
		int k;
		for (k = 0; k < nseg; k++)
			if (segs[k].tok == i) {
				if (first < 0)
					first = segs[k].dur;
				want += segs[k].dur;
			}
		want *= FS / 1000.0;
		first *= FS / 1000.0;
		if (!strcmp(name, "C") || !strcmp(name, "J")) {
			if (first <= 0 || first >= want)
				first = want * 0.4;
			w[nw] = name[0] == 'C' ? 45 : 32;
			wants[nw++] = first;
			w[nw] = name[0] == 'C' ? 41 : 37;
			wants[nw++] = want - first;
			continue;
		}
		w[nw] = pt_wave(toks[i].ph, i + 1 < ntok ? toks[i + 1].ph : 0, i ? toks[i - 1].ph : 0);
		wants[nw++] = want;
	}

	for (i = 0; i < nw; i++) {
		double want = wants[i], rate, t, end;
		int mid, k, l = i ? w[i - 1] : 0, r = i + 1 < nw ? w[i + 1] : 0, voiced;
		if (w[i] == 0) {   /* a pause */
			cur += want;
			continue;
		}
		/* this phoneme is the second half of l-it and the first half of it-r */
		voiced = w[i] < 38;
		nsig = nmarks = 0;
		pt_half(v, 1, l, w[i], voiced);
		mid = nsig;
		pt_half(v, 0, w[i], r, voiced);
		if (!nmarks)
			continue;
		/* b, d, g: the middle is the closure, then comes the release,
		 * which in some voices is only a quick start of voicing with no
		 * burst.  Stretching it would turn that into a glide ("brown"
		 * heard as "own"), so put the sound out as recorded and give the
		 * stop its length in the closure, adding silence or cutting some */
		if (w[i] >= 31 && w[i] <= 33) {
			int len = (int)lrint(want), cut = 0, gap = 0, o = (int)lrint(cur), n;
			if (len < nsig)
				cut = nsig - len < mid ? nsig - len : mid;
			else
				gap = len - nsig;
			for (j = 0, n = 0; j < nsig; j++) {
				int oi;
				if (j >= mid - cut && j < mid)
					continue;
				oi = o + n + (j >= mid ? gap : 0);
				/* fade in over 2 ms, where the sound before rings out */
				if (oi >= 0 && oi < cap)
					out[oi] += (float)(sig[j] * (n < 44 ? n / 44.0 : 1));
				n++;
			}
			cur += n + gap;
			continue;
		}
		/* TD-PSOLA: put out the recorded periods, each in a window two
		 * periods wide, at the spacing the pitch wants, repeating or
		 * dropping periods to give the phoneme its length */
		rate = want > 0 ? nsig / want : 1;
		if (rate < 0.4)
			rate = 0.4;
		if (rate > 2.5)
			rate = 2.5;
		t = 0;
		end = nsig / rate;
		while (t < end) {
			double ip = t * rate, step;
			int m = 0, P, c, o;
			for (k = 1; k < nmarks; k++)
				if (fabs(marks[k].pos - ip) < fabs(marks[m].pos - ip))
					m = k;
			P = marks[m].per;
			c = marks[m].pos;
			step = marks[m].voiced ? FS / pitch_at(cur * 1000.0 / FS) : P;
			o = (int)lrint(cur);
			for (j = -P; j < P; j++) {
				int si = c + j, oi = o + j;
				double wv;
				if (si < marks[m].lo || si >= marks[m].hi || oi < 0 || oi >= cap)
					continue;
				wv = 0.5 - 0.5 * cos(M_PI * (j + P) / P);
				out[oi] += (float)(sig[si] * wv);
			}
			t += step;
			cur += step;
		}
	}
	free(w);
	free(wants);
	*nout = (int)cur + 1 < cap ? (int)cur + 1 : cap;
	pt_lowpass(out, *nout);
	return out;
}
