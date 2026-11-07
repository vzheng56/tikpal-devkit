// 声波码: reading a code from a camera frame (yk_vc_scan; layout in README.md). Plain C99 + libm, for the apps and
// the host tests. Thread-safe: all working memory is allocated per call and freed before returning.
//
//  1. Binarise: a pixel (its 3x3 mean when the frame is noisy) is lit when brighter than the mean of a window 1/12 of
//     the frame wide plus a margin from the frame's noise (integral image), so any exposure works. If nothing is read,
//     a second try also asks each lit pixel to beat its 5x5 mean: that parts bars that blur has joined.
//  2. Marks: 8-connected runs joined with union-find; area, centroid and second moments per mark.
//  3. Centre: every bar-shaped mark points at the centre. Each votes along its axis on a coarse grid; the best
//     peaks are refined by weighted least squares of the axes (lines stay lines under tilt and perspective).
//  4. Tilt and perspective: the bars' midpoints lie on one circle (radius 0.64) whatever their lengths. Its image is a
//     conic, fitted about the centre c; that conic is exactly the image of the circle under a homography fixing c,
//     which it gives (up to a turn). In that frame the angles give the bars' phase (48-fold) and a slot per mark.
//  5. Geometry: affine, then a homography, from the slot positions to the midpoints, dropping outliers; the marks are
//     collected again through it and it is fitted once more.
//  6. Levels: each bar's extent along its own ray at half its contrast, in ring units, fitted to a + b * level.
//     Bars that are faint, off-centre, between levels or on a raised or uneven background (glare) are erased.
//  7. 12 o'clock: the coral dot at radius 0.88 between two bars must be the one bright spot of the 48 gap positions.
//     If none is (covered, or glare there), all 48 turns are tried and exactly one may decode, with more margin.
//  8. Reed-Solomon (yk_vc_decode_levels), then a stricter rule than the decoder's own: the bits the decoder did not
//     need must say that a random set of bars would decode like this less than once in 2^MIN_SURPLUS_BITS. Frames of
//     the bars' animation have the right layout with random lengths; this rule is what keeps them out (in the host
//     tests the decoder alone accepts about 1 animation frame in 900). A code of a later version passes the same
//     rule and comes back as YK_VC_LATER, without a payload.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "yk_voicecode.h"
#include "yk_voicecode_priv.h"

#ifdef YK_VC_TRACE
#include <stdio.h>
#define TRACE(...) fprintf(stderr, __VA_ARGS__)
#else
#define TRACE(...) ((void)0)
#endif

#define PI_D 3.14159265358979323846
#define SLOT (2.0 * PI_D / YK_VC_BARS)
#define BASE ((double)YK_VC_BASE_R)

// Tuning. Lengths and radii are in ring units.
#define MIN_AREA 6               // pixels: smaller marks are noise
#define MAX_LABELS (1 << 18)     // more provisional labels than this: too noisy to read (and ~13 MB of them)
#define BAR_RATIO 1.9            // longer / shorter axis of a bar-shaped mark
#define MIN_CANDIDATES 16        // bar-shaped marks needed before looking for a centre
#define MIN_VOTES 14.0f          // Hough votes for a centre
#define PEAKS 3                  // centres tried
#define MIN_GEOM_BARS 24         // marks on distinct slots needed for the geometry
#define MAX_RMS 0.02             // midpoint residual of the geometry, ring units
#define MAX_TILT 1.9             // longer / shorter axis of the bars' circle
#define MIN_READ_BARS 36         // bars whose length could be read
#define LEVEL_TOL 0.30           // a bar this far (in steps) from its level is erased
#define MIN_SURPLUS_BITS 24.0    // see 8. above

typedef struct {
    int x0, x1, label;
} run_t;

typedef struct {
    double n, sx, sy, sxx, sxy, syy;
} mom_t;

typedef struct {
    double cx, cy;    // centroid, pixels
    double ux, uy;    // longer axis, unit vector
    double len, wid;  // sqrt(12 lambda) along both axes: the extent of a uniform rectangle
    double area;
    double cxx, cxy, cyy;  // covariance
    int bar;               // bar-shaped
} mark_t;

typedef struct {
    const uint8_t *img;
    int w, h, stride;
    double noise;  // estimated sigma of the frame's noise, grey levels
    mark_t *marks;
    int nmarks;
    int *cand;  // indices of bar-shaped marks
    int ncand;
} ctx_t;

// ---- small linear algebra ----

/** Solves a x = b (n <= 9, a row-major n x n) in place: b gets x. Returns 0, or -1 when singular. */
static int solve(double *a, double *b, int n) {
    for (int c = 0; c < n; c++) {
        int p = c;
        for (int r = c + 1; r < n; r++)
            if (fabs(a[r * n + c]) > fabs(a[p * n + c])) p = r;
        if (fabs(a[p * n + c]) < 1e-12) return -1;
        if (p != c) {
            for (int k = 0; k < n; k++) {
                const double t = a[c * n + k];
                a[c * n + k] = a[p * n + k];
                a[p * n + k] = t;
            }
            const double t = b[c];
            b[c] = b[p];
            b[p] = t;
        }
        for (int r = c + 1; r < n; r++) {
            const double f = a[r * n + c] / a[c * n + c];
            if (f == 0) continue;
            for (int k = c; k < n; k++) a[r * n + k] -= f * a[c * n + k];
            b[r] -= f * b[c];
        }
    }
    for (int c = n - 1; c >= 0; c--) {
        double s = b[c];
        for (int k = c + 1; k < n; k++) s -= a[c * n + k] * b[k];
        b[c] = s / a[c * n + c];
    }
    return 0;
}

/** Model point (ring units) -> pixel. h is row-major 3 x 3. Returns 0, or -1 behind the camera. */
static int hom_apply(const double h[9], double X, double Y, double *x, double *y) {
    const double w = h[6] * X + h[7] * Y + h[8];
    if (w <= 1e-9) return -1;
    *x = (h[0] * X + h[1] * Y + h[2]) / w;
    *y = (h[3] * X + h[4] * Y + h[5]) / w;
    return 0;
}

static int inv3(const double m[9], double o[9]) {
    const double c0 = m[4] * m[8] - m[5] * m[7], c1 = m[5] * m[6] - m[3] * m[8], c2 = m[3] * m[7] - m[4] * m[6];
    const double det = m[0] * c0 + m[1] * c1 + m[2] * c2;
    if (fabs(det) < 1e-15) return -1;
    const double id = 1.0 / det;
    o[0] = c0 * id;
    o[1] = (m[2] * m[7] - m[1] * m[8]) * id;
    o[2] = (m[1] * m[5] - m[2] * m[4]) * id;
    o[3] = c1 * id;
    o[4] = (m[0] * m[8] - m[2] * m[6]) * id;
    o[5] = (m[2] * m[3] - m[0] * m[5]) * id;
    o[6] = c2 * id;
    o[7] = (m[1] * m[6] - m[0] * m[7]) * id;
    o[8] = (m[0] * m[4] - m[1] * m[3]) * id;
    return 0;
}

static double wrap_pi(double a) {
    while (a > PI_D) a -= 2 * PI_D;
    while (a < -PI_D) a += 2 * PI_D;
    return a;
}

static int cmp_float(const void *a, const void *b) {
    const float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/** The q-quantile (0..1) of v[0..n-1]; sorts v. */
static float quantile(float *v, int n, double q) {
    if (n <= 0) return 0;
    qsort(v, (size_t)n, sizeof(float), cmp_float);
    int i = (int)(q * (n - 1) + 0.5);
    if (i < 0) i = 0;
    if (i > n - 1) i = n - 1;
    return v[i];
}

// ---- 1. and 2.: binarise and label ----

static double estimate_noise(const uint8_t *img, int w, int h, int stride) {
    unsigned hist[256] = {0}, total = 0;
    for (int y = 1; y < h; y += 3) {
        const uint8_t *row = img + (size_t)y * (size_t)stride;
        for (int x = 0; x + 1 < w; x++) {
            hist[abs((int)row[x + 1] - (int)row[x])]++;
            total++;
        }
    }
    unsigned acc = 0;
    int med = 0;
    for (int i = 0; i < 256; i++) {
        acc += hist[i];
        if (2 * acc >= total) {
            med = i;
            break;
        }
    }
    return 1.4826 * med / 1.41421356;  // median |difference of two pixels| -> sigma of one
}

static int uf_find(int *parent, int a) {
    int r = a;
    while (parent[r] != r) r = parent[r];
    while (parent[a] != r) {
        const int n = parent[a];
        parent[a] = r;
        a = n;
    }
    return r;
}

typedef struct {
    int *parent;
    mom_t *mom;
    int n, cap;
} labels_t;

static int label_new(labels_t *L) {
    if (L->n == L->cap) {
        if (L->cap >= MAX_LABELS) return -1;
        const int cap = L->cap ? 2 * L->cap : 4096;
        int *p = realloc(L->parent, (size_t)cap * sizeof(int));
        if (!p) return -1;
        L->parent = p;
        mom_t *m = realloc(L->mom, (size_t)cap * sizeof(mom_t));
        if (!m) return -1;
        L->mom = m;
        L->cap = cap;
    }
    L->parent[L->n] = L->n;
    memset(&L->mom[L->n], 0, sizeof(mom_t));
    return L->n++;
}

static void add_run(mom_t *m, int x0, int x1, int y) {
    const double n = x1 - x0 + 1;
    const double sx = n * (x0 + x1) * 0.5;
    // sum of x^2 for x0..x1
    const double a = x0 - 1, b = x1;
    const double sxx = (b * (b + 1) * (2 * b + 1) - a * (a + 1) * (2 * a + 1)) / 6.0;
    m->n += n;
    m->sx += sx;
    m->sy += n * y;
    m->sxx += sxx;
    m->sxy += sx * y;
    m->syy += n * (double)y * y;
}

/** The integral image, (w + 1) x (h + 1). */
static uint32_t *integral(const ctx_t *c) {
    const size_t iw = (size_t)c->w + 1;
    uint32_t *ii = malloc(iw * ((size_t)c->h + 1) * sizeof(uint32_t));
    if (!ii) return NULL;
    memset(ii, 0, iw * sizeof(uint32_t));
    for (int y = 0; y < c->h; y++) {
        const uint8_t *row = c->img + (size_t)y * (size_t)c->stride;
        uint32_t *o = ii + (size_t)(y + 1) * iw, *u = ii + (size_t)y * iw;
        uint32_t s = 0;
        o[0] = 0;
        for (int x = 0; x < c->w; x++) {
            s += row[x];
            o[x + 1] = u[x + 1] + s;
        }
    }
    return ii;
}

/** Finds the bright marks. `ridge` (the second try) also asks each lit pixel to be brighter than its 5 x 5 mean,
 *  which parts bars that blur has joined. Returns 0, or -1 (out of memory or too noisy). */
static int find_marks(ctx_t *c, const uint32_t *ii, int ridge) {
    const int w = c->w, h = c->h;
    const size_t iw = (size_t)w + 1;
    run_t *prev = malloc(((size_t)w / 2 + 2) * sizeof(run_t));
    run_t *cur = malloc(((size_t)w / 2 + 2) * sizeof(run_t));
    labels_t L = {0};
    int status = -1;
    c->marks = NULL;
    c->cand = NULL;
    c->nmarks = c->ncand = 0;
    if (!prev || !cur) goto done;

    const int box = c->noise >= 2.5;  // compare 3x3 means when noisy
    const double sigma = box ? c->noise / 3.0 : c->noise;
    const float margin = (float)fmax(8.0, 3.0 * sigma);
    int hw = w / 24;
    if (hw < 8) hw = 8;

    int nprev = 0;
    for (int y = 0; y < h; y++) {
        const int wy0 = y - hw < 0 ? 0 : y - hw, wy1 = y + hw + 1 > h ? h : y + hw + 1;
        const uint32_t *W0 = ii + (size_t)wy0 * iw, *W1 = ii + (size_t)wy1 * iw;
        const int by0 = y - 1 < 0 ? 0 : y - 1, by1 = y + 2 > h ? h : y + 2;
        const uint32_t *B0 = ii + (size_t)by0 * iw, *B1 = ii + (size_t)by1 * iw;
        const int ry0 = y - 2 < 0 ? 0 : y - 2, ry1 = y + 3 > h ? h : y + 3;
        const uint32_t *R0 = ii + (size_t)ry0 * iw, *R1 = ii + (size_t)ry1 * iw;
        const uint8_t *row = c->img + (size_t)y * (size_t)c->stride;
        const float wrows = (float)(wy1 - wy0), brows = (float)(by1 - by0), rrows = (float)(ry1 - ry0);
        int ncur = 0, in = 0, x0 = 0;
        for (int x = 0; x <= w; x++) {
            int lit = 0;
            if (x < w) {
                const int wx0 = x - hw < 0 ? 0 : x - hw, wx1 = x + hw + 1 > w ? w : x + hw + 1;
                const float wsum = (float)(W1[wx1] - W1[wx0] - W0[wx1] + W0[wx0]);
                const float warea = wrows * (float)(wx1 - wx0);
                float v;
                if (box) {
                    const int bx0 = x - 1 < 0 ? 0 : x - 1, bx1 = x + 2 > w ? w : x + 2;
                    v = (float)(B1[bx1] - B1[bx0] - B0[bx1] + B0[bx0]) / (brows * (float)(bx1 - bx0));
                } else {
                    v = row[x];
                }
                lit = v * warea > wsum + margin * warea;
                if (lit && ridge) {
                    const int rx0 = x - 2 < 0 ? 0 : x - 2, rx1 = x + 3 > w ? w : x + 3;
                    const float rsum = (float)(R1[rx1] - R1[rx0] - R0[rx1] + R0[rx0]);
                    lit = v * rrows * (float)(rx1 - rx0) > rsum;
                }
            }
            if (lit && !in) {
                in = 1;
                x0 = x;
            } else if (!lit && in) {
                in = 0;
                cur[ncur].x0 = x0;
                cur[ncur].x1 = x - 1;
                cur[ncur].label = -1;
                ncur++;
            }
        }
        // join with the previous row (8-connected: ranges touching diagonally count)
        int p = 0;
        for (int i = 0; i < ncur; i++) {
            run_t *r = &cur[i];
            while (p < nprev && prev[p].x1 < r->x0 - 1) p++;
            for (int q = p; q < nprev && prev[q].x0 <= r->x1 + 1; q++) {
                const int lq = uf_find(L.parent, prev[q].label);
                if (r->label < 0) {
                    r->label = lq;
                } else {
                    const int lr = uf_find(L.parent, r->label);
                    if (lr != lq) {
                        if (lr < lq) L.parent[lq] = lr;
                        else L.parent[lr] = lq;
                        r->label = lr < lq ? lr : lq;
                    }
                }
            }
            if (r->label < 0) {
                r->label = label_new(&L);
                if (r->label < 0) goto done;
            }
            add_run(&L.mom[r->label], r->x0, r->x1, y);
        }
        run_t *t = prev;
        prev = cur;
        cur = t;
        nprev = ncur;
    }

    // merge moments into the roots
    for (int i = 0; i < L.n; i++) {
        const int r = uf_find(L.parent, i);
        if (r == i) continue;
        mom_t *a = &L.mom[r], *b = &L.mom[i];
        a->n += b->n;
        a->sx += b->sx;
        a->sy += b->sy;
        a->sxx += b->sxx;
        a->sxy += b->sxy;
        a->syy += b->syy;
        b->n = 0;
    }
    int count = 0;
    const double max_area = (double)w * h / 8.0;
    for (int i = 0; i < L.n; i++)
        if (L.parent[i] == i && L.mom[i].n >= MIN_AREA && L.mom[i].n <= max_area) count++;
    c->marks = malloc((size_t)(count ? count : 1) * sizeof(mark_t));
    c->cand = malloc((size_t)(count ? count : 1) * sizeof(int));
    if (!c->marks || !c->cand) goto done;
    c->nmarks = 0;
    c->ncand = 0;
    for (int i = 0; i < L.n; i++) {
        const mom_t *m = &L.mom[i];
        if (L.parent[i] != i || m->n < MIN_AREA || m->n > max_area) continue;
        mark_t *k = &c->marks[c->nmarks];
        k->area = m->n;
        k->cx = m->sx / m->n;
        k->cy = m->sy / m->n;
        k->cxx = m->sxx / m->n - k->cx * k->cx + 1.0 / 12;
        k->cyy = m->syy / m->n - k->cy * k->cy + 1.0 / 12;
        k->cxy = m->sxy / m->n - k->cx * k->cy;
        const double tr = 0.5 * (k->cxx + k->cyy), df = 0.5 * (k->cxx - k->cyy);
        const double rt = sqrt(df * df + k->cxy * k->cxy);
        const double l1 = tr + rt, l2 = fmax(tr - rt, 1.0 / 12);
        const double ang = 0.5 * atan2(2 * k->cxy, k->cxx - k->cyy);
        k->ux = cos(ang);
        k->uy = sin(ang);
        k->len = sqrt(12 * l1);
        k->wid = sqrt(12 * l2);
        const double fill = k->area / (k->len * k->wid);
        k->bar = k->len / k->wid >= BAR_RATIO && fill > 0.5 && fill < 1.4 && k->len >= 5;
        if (k->bar) c->cand[c->ncand++] = c->nmarks;
        c->nmarks++;
    }
    status = 0;
done:
    free(prev);
    free(cur);
    free(L.parent);
    free(L.mom);
    return status;
}

// ---- 3. centre ----

typedef struct {
    double x, y, votes;
} peak_t;

static int find_peaks(const ctx_t *c, peak_t *peaks) {
    const int cs = c->w >= 960 ? 4 : 2;
    const int aw = c->w / cs + 1, ah = c->h / cs + 1;
    float *acc = calloc((size_t)aw * ah, sizeof(float));
    float *sm = calloc((size_t)aw * ah, sizeof(float));
    int np = 0;
    if (!acc || !sm) goto done;
    for (int i = 0; i < c->ncand; i++) {
        const mark_t *m = &c->marks[c->cand[i]];
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            int last = -1;
            for (double t = 1.1 * m->len; t <= 9.0 * m->len; t += 0.5 * cs) {
                const double x = m->cx + sgn * t * m->ux, y = m->cy + sgn * t * m->uy;
                if (x < 0 || y < 0 || x >= c->w || y >= c->h) break;
                const int cell = (int)(y / cs) * aw + (int)(x / cs);
                if (cell != last) {
                    acc[cell] += 1;
                    last = cell;
                }
            }
        }
    }
    for (int y = 1; y < ah - 1; y++)
        for (int x = 1; x < aw - 1; x++) {
            float s = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) s += acc[(y + dy) * aw + x + dx];
            sm[y * aw + x] = s;
        }
    const int sup = 40 / cs;  // two codes' centres are further apart than this
    for (; np < PEAKS; np++) {
        int best = -1;
        float bv = 0;
        for (int i = 0; i < aw * ah; i++)
            if (sm[i] > bv) {
                bv = sm[i];
                best = i;
            }
        if (best < 0 || bv < MIN_VOTES) break;
        const int bx = best % aw, by = best / aw;
        // centre of mass of the raw votes around the peak
        double sx = 0, sy = 0, sw = 0;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) {
                const int x = bx + dx, y = by + dy;
                if (x < 0 || y < 0 || x >= aw || y >= ah) continue;
                const double v = acc[y * aw + x];
                sx += v * x;
                sy += v * y;
                sw += v;
            }
        peaks[np].x = (sx / sw + 0.5) * cs;
        peaks[np].y = (sy / sw + 0.5) * cs;
        peaks[np].votes = bv;
        for (int y = by - sup; y <= by + sup; y++)
            for (int x = bx - sup; x <= bx + sup; x++)
                if (x >= 0 && y >= 0 && x < aw && y < ah) sm[y * aw + x] = 0;
    }
done:
    free(acc);
    free(sm);
    return np;
}

/** Weighted least-squares point closest to the axes of the marks idx[0..n-1]. */
static int axes_point(const ctx_t *c, const int *idx, int n, double *px, double *py) {
    double a[4] = {0}, b[2] = {0};
    for (int i = 0; i < n; i++) {
        const mark_t *m = &c->marks[idx[i]];
        const double nx = -m->uy, ny = m->ux;
        const double dx = *px - m->cx, dy = *py - m->cy;
        const double d2 = dx * dx + dy * dy + 1;
        const double wt = m->len * m->len / d2;  // the axis's angle is good to ~1/len; its miss at the centre ~d/len
        a[0] += wt * nx * nx;
        a[1] += wt * nx * ny;
        a[3] += wt * ny * ny;
        const double pn = nx * m->cx + ny * m->cy;
        b[0] += wt * nx * pn;
        b[1] += wt * ny * pn;
    }
    a[2] = a[1];
    if (solve(a, b, 2) != 0) return -1;
    *px = b[0];
    *py = b[1];
    return 0;
}

/** The marks whose axes pass within `tol` (fraction of their distance, plus `px` pixels) of (x, y). */
static int axes_through(const ctx_t *c, double x, double y, double tol, double px, int *out) {
    int n = 0;
    for (int i = 0; i < c->ncand; i++) {
        const mark_t *m = &c->marks[c->cand[i]];
        const double dx = x - m->cx, dy = y - m->cy;
        const double d = sqrt(dx * dx + dy * dy);
        const double miss = fabs(dx * m->uy - dy * m->ux);
        if (d < 1.1 * m->len || d > 9.0 * m->len) continue;
        if (miss > tol * d + px) continue;
        out[n++] = c->cand[i];
    }
    return n;
}

// ---- 4. and 5. geometry ----

typedef struct {
    int mark;
    int slot;      // provisional slot (the phase's origin is arbitrary until the dot is found)
    double score;  // how far off its slot and circle: lower is better
} hit_t;

/** The model midpoint of slot k. */
static void slot_mid(int k, double *X, double *Y) {
    const double a = -PI_D / 2 + (k + 0.5) * SLOT;
    *X = BASE * cos(a);
    *Y = BASE * sin(a);
}

/** Least-squares affine x = c + A * m over the hits (h[6] = h[7] = 0). */
static int fit_affine(const ctx_t *c, const hit_t *hits, int n, double h[9]) {
    double a[9] = {0}, bx[3] = {0}, by[3] = {0};
    for (int i = 0; i < n; i++) {
        double X, Y;
        slot_mid(hits[i].slot, &X, &Y);
        const double v[3] = {X, Y, 1};
        const mark_t *m = &c->marks[hits[i].mark];
        for (int r = 0; r < 3; r++) {
            for (int k = 0; k < 3; k++) a[r * 3 + k] += v[r] * v[k];
            bx[r] += v[r] * m->cx;
            by[r] += v[r] * m->cy;
        }
    }
    double a2[9];
    memcpy(a2, a, sizeof a);
    if (solve(a, bx, 3) != 0 || solve(a2, by, 3) != 0) return -1;
    h[0] = bx[0];
    h[1] = bx[1];
    h[2] = bx[2];
    h[3] = by[0];
    h[4] = by[1];
    h[5] = by[2];
    h[6] = 0;
    h[7] = 0;
    h[8] = 1;
    return 0;
}

/** Homography by DLT (h[8] = 1) from the hits; h holds the previous estimate. Image coordinates are centred on its
 *  centre and scaled by its size for conditioning, and weighted by its 1/w^2 so the error is geometric. */
static int fit_homography(const ctx_t *c, const hit_t *hits, int n, double h[9]) {
    const double ox = h[2], oy = h[5];
    double s = sqrt(fabs(h[0] * h[4] - h[1] * h[3]));
    if (s < 1) s = 1;
    double prev[9];
    memcpy(prev, h, sizeof prev);
    double ata[64] = {0}, atb[8] = {0};
    for (int i = 0; i < n; i++) {
        double X, Y;
        slot_mid(hits[i].slot, &X, &Y);
        const double x = (c->marks[hits[i].mark].cx - ox) / s, y = (c->marks[hits[i].mark].cy - oy) / s;
        const double w = prev[6] * X + prev[7] * Y + prev[8];
        const double wt = 1.0 / (w * w);
        const double r1[8] = {X, Y, 1, 0, 0, 0, -X * x, -Y * x};
        const double r2[8] = {0, 0, 0, X, Y, 1, -X * y, -Y * y};
        for (int r = 0; r < 8; r++) {
            for (int k = 0; k < 8; k++) ata[r * 8 + k] += wt * (r1[r] * r1[k] + r2[r] * r2[k]);
            atb[r] += wt * (r1[r] * x + r2[r] * y);
        }
    }
    if (solve(ata, atb, 8) != 0) return -1;
    // undo the conditioning: x = s * x' + ox
    const double g[9] = {atb[0], atb[1], atb[2], atb[3], atb[4], atb[5], atb[6], atb[7], 1};
    h[0] = s * g[0] + ox * g[6];
    h[1] = s * g[1] + ox * g[7];
    h[2] = s * g[2] + ox * g[8];
    h[3] = s * g[3] + oy * g[6];
    h[4] = s * g[4] + oy * g[7];
    h[5] = s * g[5] + oy * g[8];
    h[6] = g[6];
    h[7] = g[7];
    h[8] = 1;
    return 0;
}

/** Residuals of the hits under h, in pixels; returns the RMS. */
static double residuals(const ctx_t *c, const hit_t *hits, int n, const double h[9], double *res) {
    double ss = 0;
    for (int i = 0; i < n; i++) {
        double X, Y, x, y;
        slot_mid(hits[i].slot, &X, &Y);
        if (hom_apply(h, X, Y, &x, &y) != 0) {
            res[i] = 1e9;
            ss += 1e9;
            continue;
        }
        const double dx = x - c->marks[hits[i].mark].cx, dy = y - c->marks[hits[i].mark].cy;
        res[i] = sqrt(dx * dx + dy * dy);
        ss += res[i] * res[i];
    }
    return n ? sqrt(ss / n) : 1e9;
}

/** Ring radius in pixels (longer axis) and tilt, from the homography's Jacobian at the centre. */
static void jacobian_axes(const double h[9], double *major, double *minor) {
    const double a = h[0] - h[2] * h[6], b = h[1] - h[2] * h[7];
    const double cc = h[3] - h[5] * h[6], d = h[4] - h[5] * h[7];
    const double t = a * a + b * b + cc * cc + d * d, det = fabs(a * d - b * cc);
    const double q = sqrt(fmax(t * t - 4 * det * det, 0));
    *major = sqrt(0.5 * (t + q));
    *minor = sqrt(fmax(0.5 * (t - q), 1e-12));
}

/** Expected areas of a bar of level 0 and 3, ring units. */
#define AREA0 (YK_VC_LEN0 * YK_VC_BAR_W + PI_D * 0.25 * YK_VC_BAR_W * YK_VC_BAR_W)
#define AREA3 ((YK_VC_LEN0 + 3 * YK_VC_LEN_STEP) * YK_VC_BAR_W + PI_D * 0.25 * YK_VC_BAR_W * YK_VC_BAR_W)

/** Every mark that can be a bar of the code whose pixel -> model map is hinv: within rtol (relative) of the bars'
 *  circle, bar-shaped and radial, given the slot of its angle. Keeps the best mark per slot. Returns the hits. */
static int collect(const ctx_t *c, const double hinv[9], double rtol, hit_t *hits, int *best_of_slot) {
    for (int k = 0; k < YK_VC_BARS; k++) best_of_slot[k] = -1;
    int n = 0;
    for (int i = 0; i < c->nmarks; i++) {
        const mark_t *m = &c->marks[i];
        const double w = hinv[6] * m->cx + hinv[7] * m->cy + hinv[8];
        if (w <= 1e-9) continue;
        const double X = (hinv[0] * m->cx + hinv[1] * m->cy + hinv[2]) / w;
        const double Y = (hinv[3] * m->cx + hinv[4] * m->cy + hinv[5]) / w;
        const double r = sqrt(X * X + Y * Y);
        if (fabs(r / BASE - 1) > rtol) continue;
        // the mark's shape in model units: J = d(model)/d(pixel) at the mark
        const double j00 = (hinv[0] - X * hinv[6]) / w, j01 = (hinv[1] - X * hinv[7]) / w;
        const double j10 = (hinv[3] - Y * hinv[6]) / w, j11 = (hinv[4] - Y * hinv[7]) / w;
        const double area = m->area * fabs(j00 * j11 - j01 * j10);
        if (area < 0.3 * AREA0 || area > 2.5 * AREA3) continue;
        // covariance in model units: J C J^T
        const double a = j00 * m->cxx + j01 * m->cxy, b = j00 * m->cxy + j01 * m->cyy;
        const double cc = j10 * m->cxx + j11 * m->cxy, d = j10 * m->cxy + j11 * m->cyy;
        const double mxx = a * j00 + b * j01, mxy = a * j10 + b * j11, myy = cc * j10 + d * j11;
        const double tr = 0.5 * (mxx + myy), df = 0.5 * (mxx - myy), rt = sqrt(df * df + mxy * mxy);
        const double l1 = tr + rt, l2 = fmax(tr - rt, 1e-12);
        if (12 * l1 > 0.42 * 0.42) continue;  // longer than any bar
        if (l1 / l2 < 1.4 * 1.4) continue;     // not bar-shaped
        const double ang = 0.5 * atan2(2 * mxy, mxx - myy);
        const double radial = fabs(wrap_pi(2 * (ang - atan2(Y, X)))) * 0.5;  // axis vs. ray, 0..pi/2
        if (radial > 15 * PI_D / 180) continue;
        const double f = wrap_pi(atan2(Y, X) + PI_D / 2) / SLOT - 0.5;
        double fk = floor(f + 0.5);
        const double res = f - fk;
        if (fabs(res) > 0.3) continue;
        int k = ((int)fk % YK_VC_BARS + YK_VC_BARS) % YK_VC_BARS;
        const double score = fabs(res) + fabs(r / BASE - 1) / rtol;
        if (best_of_slot[k] >= 0) {
            hit_t *o = &hits[best_of_slot[k]];
            if (score < o->score) {
                o->mark = i;
                o->score = score;
            }
            continue;
        }
        best_of_slot[k] = n;
        hits[n].mark = i;
        hits[n].slot = k;
        hits[n].score = score;
        n++;
    }
    return n;
}

/** Fits the geometry to the hits, dropping outliers; h holds an initial estimate and gets the result: affine first,
 *  then a homography where it is clearly better (perspective is small). Returns the hits kept (compacted), or -1.
 *  rms gets the residual in ring units. */
static int fit_geometry(const ctx_t *c, hit_t *hits, int n, double h[9], double *rms) {
    double *res = malloc((size_t)(n ? n : 1) * sizeof(double));
    double *res2 = malloc((size_t)(n ? n : 1) * sizeof(double));
    int ok = -1;
    if (!res || !res2) goto done;
    for (int pass = 0; pass < 5; pass++) {
        if (n < MIN_GEOM_BARS) goto done;
        double g[9];
        if (fit_affine(c, hits, n, g) != 0) goto done;
        double r = residuals(c, hits, n, g, res);
        if (pass >= 2) {
            double gh[9];
            memcpy(gh, g, sizeof gh);
            for (int it = 0; it < 2; it++)
                if (fit_homography(c, hits, n, gh) != 0) break;
            const double rh = residuals(c, hits, n, gh, res2);
            if (rh < 0.85 * r) {
                memcpy(g, gh, sizeof g);
                memcpy(res, res2, (size_t)n * sizeof(double));
                r = rh;
            }
        }
        double major, minor;
        jacobian_axes(g, &major, &minor);
        if (major < 4) goto done;
        memcpy(h, g, sizeof g);
        *rms = r / major;
        // drop outliers: well beyond the RMS, and never more than most of a bar's width off
        const double lim = pass < 2 ? fmax(2.5 * r, 0.08 * major) : fmin(fmax(1.0, 2.5 * r), fmax(1.0, 0.025 * major));
        int m = 0;
        for (int i = 0; i < n; i++)
            if (res[i] <= lim) hits[m++] = hits[i];
        if (m == n && pass >= 2) break;
        n = m;
    }
    if (n < MIN_GEOM_BARS) goto done;
    {
        double major, minor;
        jacobian_axes(h, &major, &minor);
        *rms = residuals(c, hits, n, h, res) / major;
    }
    ok = n;
done:
    free(res);
    free(res2);
    return ok;
}

// ---- sampling ----

/** Bilinear sample at pixel (x, y); -1 outside the frame. */
static float sample(const ctx_t *c, double x, double y) {
    if (x < 0 || y < 0 || x > c->w - 1.001 || y > c->h - 1.001) return -1;
    const int x0 = (int)x, y0 = (int)y;
    const float fx = (float)(x - x0), fy = (float)(y - y0);
    const uint8_t *p = c->img + (size_t)y0 * (size_t)c->stride + x0;
    const float a = p[0] + fx * (p[1] - p[0]);
    const float b = p[c->stride] + fx * (p[c->stride + 1] - p[c->stride]);
    return a + fy * (b - a);
}

static float sample_model(const ctx_t *c, const double h[9], double X, double Y) {
    double x, y;
    if (hom_apply(h, X, Y, &x, &y) != 0) return -1;
    return sample(c, x, y);
}

// ---- 7. the dot ----

/** Finds which of the 48 gaps holds the dot: returns m (the dot sits between provisional slots m-1 and m), or -1
 *  when no gap, or more than one, is bright. bar_contrast is the bars' typical contrast. */
static int find_dot(const ctx_t *c, const double h[9], double bar_contrast) {
    double score[YK_VC_BARS];
    const double r = YK_VC_DOT_R, o = 0.5 * YK_VC_DOT_SIZE;
    for (int m = 0; m < YK_VC_BARS; m++) {
        const double g = -PI_D / 2 + m * SLOT;
        const double ux = cos(g), uy = sin(g);
        double d = 0, b = 0;
        int bad = 0;
        const double pts[5][2] = {{0, 0}, {o, 0}, {-o, 0}, {0, o}, {0, -o}};
        for (int i = 0; i < 5; i++) {
            const float v = sample_model(c, h, (r + pts[i][0]) * ux - pts[i][1] * uy, (r + pts[i][0]) * uy + pts[i][1] * ux);
            if (v < 0) bad = 1;
            d += v;
        }
        for (int s = -1; s <= 1; s += 2) {
            const double a = g + s * 0.5 * SLOT;
            const float v = sample_model(c, h, r * cos(a), r * sin(a));
            if (v < 0) bad = 1;
            b += v;
        }
        score[m] = bad ? -1e9 : d / 5 - b / 2;
    }
    int best = -1;
    double bv = -1e9, second = -1e9;
    for (int m = 0; m < YK_VC_BARS; m++) {
        if (score[m] > bv) {
            second = bv;
            bv = score[m];
            best = m;
        } else if (score[m] > second) {
            second = score[m];
        }
    }
    if (best < 0 || bv < 0.2 * bar_contrast || second > 0.5 * bv) return -1;
    return best;
}

// ---- 6. levels ----

#define PR0 0.40
#define PRSTEP 0.004
#define PRN 126  // 0.40 .. 0.90

typedef struct {
    float len, mid, contrast;
    float bg, spread;  // background beside the bar (gaps on both sides) and its spread along the ray
    int ok;
} bar_read_t;

/** Reads every bar's length along its ray under h (true slots). Returns the bars' median contrast. */
static double read_bars(const ctx_t *c, const double h[9], bar_read_t *bars) {
    float prof[PRN], gap[YK_VC_BARS][PRN];
    float bg[2 * PRN];
    for (int k = 0; k < YK_VC_BARS; k++) {
        const double a = -PI_D / 2 + (k + 1) * SLOT;  // the gap after bar k
        for (int j = 0; j < PRN; j++) {
            const double r = PR0 + j * PRSTEP;
            gap[k][j] = sample_model(c, h, r * cos(a), r * sin(a));
        }
    }
    float contrasts[YK_VC_BARS];
    int nc = 0;
    for (int k = 0; k < YK_VC_BARS; k++) {
        bar_read_t *br = &bars[k];
        br->ok = 0;
        br->len = br->mid = br->contrast = 0;
        const double a = -PI_D / 2 + (k + 0.5) * SLOT;
        const double ux = cos(a), uy = sin(a), lat = 0.007;
        int bad = 0;
        float raw[PRN];
        for (int j = 0; j < PRN && !bad; j++) {
            const double r = PR0 + j * PRSTEP;
            float v = -1;
            for (int s = -1; s <= 1; s++) {
                const float t = sample_model(c, h, r * ux - s * lat * uy, r * uy + s * lat * ux);
                if (t < 0) bad = 1;
                if (t > v) v = t;
            }
            raw[j] = v;
        }
        if (bad) continue;
        for (int j = 0; j < PRN; j++) {
            const float l = raw[j > 0 ? j - 1 : 0], r = raw[j < PRN - 1 ? j + 1 : PRN - 1];
            prof[j] = 0.25f * l + 0.5f * raw[j] + 0.25f * r;
        }
        // background: the gaps on both sides, over the bars' span
        int nb = 0;
        const int kp = (k + YK_VC_BARS - 1) % YK_VC_BARS;
        for (int j = 0; j < PRN; j++) {
            const double r = PR0 + j * PRSTEP;
            if (r < 0.47 || r > 0.81) continue;
            if (gap[kp][j] < 0 || gap[k][j] < 0) {
                bad = 1;
                break;
            }
            bg[nb++] = gap[kp][j];
            bg[nb++] = gap[k][j];
        }
        if (bad || nb == 0) continue;
        const float b = quantile(bg, nb, 0.3);
        br->bg = b;
        br->spread = bg[(int)(0.9 * (nb - 1))] - bg[(int)(0.1 * (nb - 1))];  // bg is sorted now
        // peak about the midpoint (every level covers 0.60..0.68)
        int jp = -1;
        float p = -1;
        for (int j = 0; j < PRN; j++) {
            const double r = PR0 + j * PRSTEP;
            if (r < 0.60 || r > 0.68) continue;
            if (prof[j] > p) {
                p = prof[j];
                jp = j;
            }
        }
        const float con = p - b;
        br->contrast = con;
        if (con <= 0) continue;
        const float t = b + 0.5f * con;
        int jo = jp, ji = jp;
        while (jo < PRN - 1 && prof[jo + 1] >= t) jo++;
        while (ji > 0 && prof[ji - 1] >= t) ji--;
        if (jo == PRN - 1 || ji == 0) continue;  // runs into the logo, the dot or something else
        const double ro = PR0 + (jo + (prof[jo] - t) / (prof[jo] - prof[jo + 1])) * PRSTEP;
        const double ri = PR0 + (ji - (prof[ji] - t) / (prof[ji] - prof[ji - 1])) * PRSTEP;
        br->len = (float)(ro - ri);
        br->mid = (float)(0.5 * (ro + ri));
        // beyond both ends the ray must be dark again
        double out = 0, inn = 0;
        int no = 0, ni = 0;
        for (int j = 0; j < PRN; j++) {
            const double r = PR0 + j * PRSTEP;
            if (r > ro + 0.02 && r < ro + 0.04) {
                out += prof[j];
                no++;
            }
            if (r < ri - 0.02 && r > ri - 0.04) {
                inn += prof[j];
                ni++;
            }
        }
        if (no) out /= no;
        if (ni) inn /= ni;
        if ((no && out > b + 0.35 * con) || (ni && inn > b + 0.35 * con)) continue;
        br->ok = 1;
        contrasts[nc++] = con;
    }
    const double med = nc ? quantile(contrasts, nc, 0.5) : 0;
    // the code's own background: glare raises it locally, and its edge makes it uneven along a ray
    // (blur spreads every bar's halo into the gaps, so both are judged against the code's own median)
    float bgs[YK_VC_BARS], sps[YK_VC_BARS];
    int nbg = 0;
    for (int k = 0; k < YK_VC_BARS; k++)
        if (bars[k].ok) {
            bgs[nbg] = bars[k].bg;
            sps[nbg++] = bars[k].spread;
        }
    const double bg0 = nbg ? quantile(bgs, nbg, 0.5) : 0;
    const double uneven = (nbg ? quantile(sps, nbg, 0.5) : 0) + fmax(0.25 * med, 5 * c->noise);
    for (int k = 0; k < YK_VC_BARS; k++) {
        bar_read_t *br = &bars[k];
        if (!br->ok) continue;
        if (br->contrast < 0.3 * med || br->contrast < 4 * c->noise + 4) br->ok = 0;
        if (fabs(br->mid - BASE) > 0.03) br->ok = 0;
        if (br->bg > bg0 + 0.15 * med || br->spread > uneven) br->ok = 0;
    }
    return med;
}

/** Lengths -> levels: fits len = a + b * level, robustly. Returns the bars read, or -1 when no fit is sane. */
static int assign_levels(const bar_read_t *bars, int8_t levels[YK_VC_BARS]) {
    double best = 1e18, ba = 0, bb = 0;
    for (double b = 0.055; b <= 0.0851; b += 0.0025) {
        for (double a = 0.05; a <= 0.18; a += 0.0025) {
            double cost = 0;
            for (int k = 0; k < YK_VC_BARS; k++) {
                if (!bars[k].ok) continue;
                const double f = (bars[k].len - a) / b;
                double e;
                if (f < -0.5) e = 0.25 + (-0.5 - f);
                else if (f > 3.5) e = 0.25 + (f - 3.5);
                else {
                    const double r = f - floor(f + 0.5);
                    e = fmin(r * r, 0.25);
                }
                cost += e;
            }
            if (cost < best) {
                best = cost;
                ba = a;
                bb = b;
            }
        }
    }
    // refine: least squares over the bars close to their level
    for (int it = 0; it < 3; it++) {
        double s0 = 0, s1 = 0, s2 = 0, t0 = 0, t1 = 0;
        for (int k = 0; k < YK_VC_BARS; k++) {
            if (!bars[k].ok) continue;
            const double f = (bars[k].len - ba) / bb;
            const double l = floor(f + 0.5);
            if (l < 0 || l > 3 || fabs(f - l) > 0.25) continue;
            s0 += 1;
            s1 += l;
            s2 += l * l;
            t0 += bars[k].len;
            t1 += l * bars[k].len;
        }
        const double det = s0 * s2 - s1 * s1;
        if (s0 < 8 || fabs(det) < 1e-9) break;
        const double b = (s0 * t1 - s1 * t0) / det, a = (t0 - b * s1) / s0;
        if (b < 0.045 || b > 0.095 || a < 0.03 || a > 0.2) return -1;
        ba = a;
        bb = b;
    }
    int read = 0;
    for (int k = 0; k < YK_VC_BARS; k++) {
        levels[k] = -1;
        if (!bars[k].ok) continue;
        const double f = (bars[k].len - ba) / bb;
        const double l = floor(f + 0.5);
        if (l < 0 || l > 3 || fabs(f - l) > LEVEL_TOL) continue;
        levels[k] = (int8_t)l;
        read++;
    }
    return read;
}

// ---- 8. how much the decode is worth ----

static double log2_binom(int n, int k) {
    double s = 0;
    for (int i = 0; i < k; i++) s += log2((double)(n - i) / (i + 1));
    return s;
}

/** log2 of the chance that random bars, read with these erasures, decode at all: the decoded codeword compared with
 *  what was read. Lower is safer. */
static double surplus_bits(const int8_t read[YK_VC_BARS], const uint8_t truth[YK_VC_BARS]) {
    int erased = 0, errors = 0, known = 0;
    for (int j = 0; j < YK_VC_BYTES; j++) {
        int unread = 0, wrong = 0, right = 0;
        for (int t = 0; t < 4; t++) {
            const int k = 4 * j + t;
            if (read[k] < 0) unread++;
            else if (read[k] != truth[k]) wrong++;
            else right++;
        }
        if (unread && !wrong) {
            erased++;
            known += right;  // each bar read in an erased byte had to agree: 2 bits
        } else if (wrong) {
            errors++;
        }
    }
    const int kept = YK_VC_BYTES - erased;
    // codewords (2^48) x ball of `errors` around each / words of `kept` bytes x the known bars agreeing
    return 48.0 + log2_binom(kept, errors) + errors * log2(255.0) - 8.0 * kept - 2.0 * known;
}

// ---- the reader ----

/** One centre hypothesis, through to the payload. Returns the status reached. */
static yk_vc_status_t try_centre(const ctx_t *c, double x0, double y0, yk_vc_result_t *res) {
    const int nc = c->ncand;
    int *idx = malloc((size_t)(nc ? nc : 1) * sizeof(int));
    hit_t *hits = malloc((size_t)(c->nmarks ? c->nmarks : 1) * sizeof(hit_t));
    yk_vc_status_t st = YK_VC_NO_CENTRE;
    if (!idx || !hits) goto done;

    // 3. refine the centre on the axes through it
    double cx = x0, cy = y0;
    int n = axes_through(c, cx, cy, 0.08, 6, idx);
    TRACE("peak (%.1f, %.1f): %d axes through\n", x0, y0, n);
    if (n < MIN_CANDIDATES) goto done;
    for (int it = 0; it < 4; it++) {
        if (axes_point(c, idx, n, &cx, &cy) != 0) goto done;
        n = axes_through(c, cx, cy, it < 2 ? 0.05 : 0.035, it < 2 ? 3 : 2, idx);
        TRACE("  centre (%.1f, %.1f): %d axes\n", cx, cy, n);
        if (n < MIN_CANDIDATES) goto done;
    }

    // 4. the bars' midpoints: first keep those at a plausible distance from the centre
    {
        float *dist = malloc((size_t)n * sizeof(float));
        if (!dist) goto done;
        for (int i = 0; i < n; i++) dist[i] = (float)hypot(c->marks[idx[i]].cx - cx, c->marks[idx[i]].cy - cy);
        float *tmp = malloc((size_t)n * sizeof(float));
        if (!tmp) {
            free(dist);
            goto done;
        }
        memcpy(tmp, dist, (size_t)n * sizeof(float));
        const float med = quantile(tmp, n, 0.5);
        free(tmp);
        int m = 0;
        for (int i = 0; i < n; i++)
            if (dist[i] > 0.65f * med && dist[i] < 1.5f * med) idx[m++] = idx[i];
        free(dist);
        n = m;
    }
    // The midpoints' conic about c: y^T K y + k^T y = 1 (y = p - c) is exactly the image of the circle |v| = BASE
    // under the homography fixing c, v = T y / (1 - g^T y), with g = k / 2 and T^T T / BASE^2 = K + g g^T.
    // Centred (g = 0) on the first pass, general after; marks off the conic are dropped.
    double tm[4] = {0}, g[2] = {0, 0};  // T (y -> v up to a turn), g
    {
        double sc = 0;
        for (int i = 0; i < n; i++) sc += hypot(c->marks[idx[i]].cx - cx, c->marks[idx[i]].cy - cy);
        sc = n ? sc / n : 1;  // conditioning: y / sc
        static const double lims[4] = {0.2, 0.12, 0.07, 0.05};
        for (int pass = 0; pass < 4; pass++) {
            if (n < MIN_CANDIDATES) goto done;
            const int np = pass == 0 ? 3 : 5;
            double a[25] = {0}, b[5] = {0};
            for (int i = 0; i < n; i++) {
                const double y1 = (c->marks[idx[i]].cx - cx) / sc, y2 = (c->marks[idx[i]].cy - cy) / sc;
                const double v[5] = {y1 * y1, 2 * y1 * y2, y2 * y2, y1, y2};
                for (int r = 0; r < np; r++) {
                    for (int k = 0; k < np; k++) a[r * np + k] += v[r] * v[k];
                    b[r] += v[r];
                }
            }
            if (solve(a, b, np) != 0) goto done;
            const double g1 = np == 5 ? 0.5 * b[3] / sc : 0, g2 = np == 5 ? 0.5 * b[4] / sc : 0;
            const double m00 = b[0] / (sc * sc) + g1 * g1, m01 = b[1] / (sc * sc) + g1 * g2, m11 = b[2] / (sc * sc) + g2 * g2;
            const double det = m00 * m11 - m01 * m01;
            if (m00 <= 0 || m11 <= 0 || det <= 0) goto done;
            // T = BASE sqrt(M), the symmetric root
            const double sd = sqrt(det), st2 = sqrt(m00 + m11 + 2 * sd);
            tm[0] = BASE * (m00 + sd) / st2;
            tm[1] = tm[2] = BASE * m01 / st2;
            tm[3] = BASE * (m11 + sd) / st2;
            g[0] = g1;
            g[1] = g2;
            int m = 0;
            for (int i = 0; i < n; i++) {
                const double y1 = c->marks[idx[i]].cx - cx, y2 = c->marks[idx[i]].cy - cy;
                const double w = 1 - g1 * y1 - g2 * y2;
                if (w < 0.5) continue;
                const double rho = hypot(tm[0] * y1 + tm[1] * y2, tm[2] * y1 + tm[3] * y2) / w / BASE;
                if (fabs(rho - 1) < lims[pass]) idx[m++] = idx[i];
            }
            n = m;
            TRACE("  conic pass %d: %d (g = %.2g %.2g)\n", pass, n, g1 * sc, g2 * sc);
        }
        if (n < MIN_CANDIDATES) goto done;
        // tilt: T's axes
        const double tr = 0.5 * (tm[0] + tm[3]), df = sqrt(0.25 * (tm[0] - tm[3]) * (tm[0] - tm[3]) + tm[1] * tm[1]);
        if (tr - df <= 0 || (tr + df) / (tr - df) > MAX_TILT) goto done;
    }
    // phase of the bars: 48 alpha = pi + 48 phi (mod 2 pi) for bars at -pi/2 + (k + 0.5) SLOT + phi
    double sr = 0, si = 0;
    for (int i = 0; i < n; i++) {
        const mark_t *m = &c->marks[idx[i]];
        const double y1 = m->cx - cx, y2 = m->cy - cy;
        const double a = YK_VC_BARS * atan2(tm[2] * y1 + tm[3] * y2, tm[0] * y1 + tm[1] * y2);
        sr += cos(a);
        si += sin(a);
    }
    TRACE("  phase coherence %.2f\n", hypot(sr, si) / n);
    if (hypot(sr, si) < 0.5 * n) goto done;  // not 48 evenly spaced marks
    const double phase = (atan2(si, sr) - PI_D) / YK_VC_BARS;

    // 5. geometry: collect through the conic's frame, fit, collect again through the fit, fit again
    int best_of_slot[YK_VC_BARS];
    double h[9];
    {
        // pixel -> provisional model: R(-phase) [T 0; -g^T 1] (p - c)
        const double cp = cos(phase), sp = sin(phase);
        const double r00 = cp * tm[0] + sp * tm[2], r01 = cp * tm[1] + sp * tm[3];
        const double r10 = -sp * tm[0] + cp * tm[2], r11 = -sp * tm[1] + cp * tm[3];
        const double hi[9] = {r00, r01, -(r00 * cx + r01 * cy), r10, r11, -(r10 * cx + r11 * cy),
                              -g[0], -g[1], 1 + g[0] * cx + g[1] * cy};
        if (inv3(hi, h) != 0) goto done;
        for (int i = 0; i < 9; i++) h[i] /= h[8];
    }
    double hinv[9], rms = 1;
    for (int round = 0; round < 2; round++) {
        if (inv3(h, hinv) != 0) goto done;
        n = collect(c, hinv, round == 0 ? 0.14 : 0.06, hits, best_of_slot);
        TRACE("  round %d: collected %d\n", round, n);
        if (n < MIN_GEOM_BARS) goto done;
        n = fit_geometry(c, hits, n, h, &rms);
        TRACE("  round %d: fitted %d rms %.4f\n", round, n, rms);
        if (n < 0) goto done;
    }
    if (rms > MAX_RMS) goto done;
    double major, minor;
    jacobian_axes(h, &major, &minor);
    if (major / minor > MAX_TILT) goto done;
    res->bars = n;
    res->radius = (float)major;
    res->tilt = (float)(major / minor);
    hom_apply(h, 0, 0, &cx, &cy);
    res->centre_x = (float)cx;
    res->centre_y = (float)cy;

    // 6. lengths -> levels, in the provisional frame (before the dot: a turn only relabels the bars)
    st = YK_VC_BAD_LEVELS;
    bar_read_t bars[YK_VC_BARS];
    const double bar_contrast = read_bars(c, h, bars);
    int8_t prov[YK_VC_BARS];
    const int read = assign_levels(bars, prov);
    if (read < 0) goto done;
    res->erased = YK_VC_BARS - read;
    memcpy(res->levels, prov, sizeof prov);
    if (read < MIN_READ_BARS) goto done;

    // 7. the dot fixes the turn: true bar k is provisional bar k + dot. With no single bright gap (the dot covered,
    //    or glare at its radius), every turn is tried and exactly one may decode, with log2(48) bits more margin.
    // 8. decode, and judge what the decoder did not need
    st = YK_VC_NO_DOT;
    if (bar_contrast <= 2) goto done;
    const int dot = find_dot(c, h, bar_contrast);
    if (dot >= 0)
        for (int k = 0; k < YK_VC_BARS; k++) res->levels[k] = prov[(k + dot) % YK_VC_BARS];
    int decodes = 0, best_later = 0;
    yk_vc_payload_t best;
    int best_corrected = 0;
    memset(&best, 0, sizeof best);
    for (int m = 0; m < YK_VC_BARS; m++) {
        if (dot >= 0 && m != dot) continue;
        int8_t lv[YK_VC_BARS];
        for (int k = 0; k < YK_VC_BARS; k++) lv[k] = prov[(k + m) % YK_VC_BARS];
        yk_vc_payload_t p;
        int corrected = 0;
        const int d = yk_vc_decode_levels(lv, &p, &corrected);
        if (d != 0 && d != YK_VC_DECODE_LATER) continue;
        // A later version passes the same rule, on the repaired codeword (its payload is not ours to read).
        uint8_t truth[YK_VC_BARS], cw[YK_VC_BYTES];
        if (d == 0) {
            if (yk_vc_encode(&p, truth) != 0) continue;
        } else {
            if (yk_vc_decode_codeword(lv, cw) < 0) continue;
            yk_vc_encode_data(cw, truth);
            memset(&p, 0, sizeof p);
        }
        const double need = MIN_SURPLUS_BITS + (dot >= 0 ? 0 : log2((double)YK_VC_BARS));
        if (surplus_bits(lv, truth) > -need) continue;
        decodes++;
        best = p;
        best_later = d != 0;
        best_corrected = corrected;
        memcpy(res->levels, lv, sizeof lv);
    }
    if (dot >= 0) st = YK_VC_NOT_DECODED;
    if (decodes != 1) goto done;
    res->payload = best;
    res->corrected = best_corrected;
    st = best_later ? YK_VC_LATER : YK_VC_OK;
done:
    free(idx);
    free(hits);
    return st;
}

yk_vc_status_t yk_vc_scan(const uint8_t *luma, int width, int height, int stride, yk_vc_result_t *result) {
    yk_vc_result_t r;
    memset(&r, 0, sizeof r);
    memset(r.levels, -1, sizeof r.levels);
    r.status = YK_VC_NO_BARS;
    if (!luma || width < 16 || height < 16 || stride < width) {
        if (result) *result = r;
        return r.status;
    }
    ctx_t c = {.img = luma, .w = width, .h = height, .stride = stride};
    uint32_t *ii = integral(&c);
    c.noise = estimate_noise(luma, width, height, stride);
    // two tries: plain binarisation, then the ridge test for bars that blur has joined
    for (int ridge = 0; ii && ridge < 2 && r.status != YK_VC_OK && r.status != YK_VC_LATER; ridge++) {
        yk_vc_result_t pass = r;
        pass.status = YK_VC_NO_BARS;
        if (find_marks(&c, ii, ridge) == 0) {
            pass.marks = c.nmarks;
            if (c.ncand >= MIN_CANDIDATES) {
                peak_t peaks[PEAKS];
                const int np = find_peaks(&c, peaks);
                pass.status = YK_VC_NO_CENTRE;
                // keep the hypothesis that got furthest (yk_vc_status_stage: the numbers are not in that order)
                for (int i = 0; i < np; i++) {
                    yk_vc_result_t t = pass;
                    const yk_vc_status_t st = try_centre(&c, peaks[i].x, peaks[i].y, &t);
                    t.status = st;
                    if (i == 0 || yk_vc_status_stage(st) > yk_vc_status_stage(pass.status)) pass = t;
                    if (st == YK_VC_OK || st == YK_VC_LATER) break;
                }
            }
        }
        free(c.marks);
        free(c.cand);
        if (ridge == 0 || yk_vc_status_stage(pass.status) > yk_vc_status_stage(r.status)) r = pass;
    }
    free(ii);
    if (r.status != YK_VC_OK) memset(&r.payload, 0, sizeof r.payload);
    if (result) *result = r;
    return r.status;
}

int yk_vc_status_stage(yk_vc_status_t status) {
    switch (status) {
        case YK_VC_NO_BARS: return 0;
        case YK_VC_NO_CENTRE: return 1;
        case YK_VC_BAD_LEVELS: return 2;  // 6. lengths -> levels
        case YK_VC_NO_DOT: return 3;      // 7. the dot (after the levels, though its number is smaller)
        case YK_VC_NOT_DECODED: return 4;
        case YK_VC_LATER: return 5;
        case YK_VC_OK: return 6;
    }
    return -1;
}

const char *yk_vc_status_name(yk_vc_status_t status) {
    switch (status) {
        case YK_VC_OK: return "ok";
        case YK_VC_NO_BARS: return "no_bars";
        case YK_VC_NO_CENTRE: return "no_centre";
        case YK_VC_NO_DOT: return "no_dot";
        case YK_VC_BAD_LEVELS: return "bad_levels";
        case YK_VC_NOT_DECODED: return "not_decoded";
        case YK_VC_LATER: return "later";
    }
    return "unknown";
}
