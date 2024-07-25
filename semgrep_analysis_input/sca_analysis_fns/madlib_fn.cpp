/* Increase the size of the buffer pointed to by p to at least n symbols.
 * If insufficient memory, returns NULL and frees the old buffer.
 */
static symbol * increase_size(symbol * p, int n) {
    symbol * q;
    int new_size = n + 20;
    void * mem = realloc((char *) p - HEAD,
                         HEAD + (new_size + 1) * sizeof(symbol));
    if (mem == NULL) {
        lose_s(p);
        return NULL;
    }
    q = (symbol *) (HEAD + (char *)mem);
    CAPACITY(q) = new_size;
    return q;
}

static int r_mark_regions(struct SN_env * z) {
    z->I[0] = z->l;
    {   int c1 = z->c; /* or, line 51 */
        if (in_grouping(z, g_v, 97, 252, 0)) goto lab1;
        if (in_grouping(z, g_v, 97, 252, 1) < 0) goto lab1; /* goto */ /* non v, line 48 */
        {   int c2 = z->c; /* or, line 49 */
            if (z->c + 1 >= z->l || z->p[z->c + 1] >> 5 != 3 || !((101187584 >> (z->p[z->c + 1] & 0x1f)) & 1)) goto lab3;
            if (!(find_among(z, a_0, 8))) goto lab3; /* among, line 49 */
            goto lab2;
        lab3:
            z->c = c2;
            if (z->c >= z->l) goto lab1;
            z->c++; /* next, line 49 */
        }
    lab2:
        z->I[0] = z->c; /* setmark p1, line 50 */
        goto lab0;
    lab1:
        z->c = c1;
        if (out_grouping(z, g_v, 97, 252, 0)) return 0;
        {    /* gopast */ /* grouping v, line 53 */
            int ret = out_grouping(z, g_v, 97, 252, 1);
            if (ret < 0) return 0;
            z->c += ret;
        }
        z->I[0] = z->c; /* setmark p1, line 53 */
    }
lab0:
    return 1;
}

static int r_R1(struct SN_env * z) {
    if (!(z->I[0] <= z->c)) return 0;
    return 1;
}

extern int in_grouping(struct SN_env * z, const unsigned char * s, int min, int max, int repeat) {
    do {
	int ch;
	if (z->c >= z->l) return -1;
	ch = z->p[z->c];
	if (ch > max || (ch -= min) < 0 || (s[ch >> 3] & (0X1 << (ch & 0X7))) == 0)
	    return 1;
	z->c++;
    } while (repeat);
    return 0;
}

extern int in_grouping_b(struct SN_env * z, const unsigned char * s, int min, int max, int repeat) {
    do {
	int ch;
	if (z->c <= z->lb) return -1;
	ch = z->p[z->c - 1];
	if (ch > max || (ch -= min) < 0 || (s[ch >> 3] & (0X1 << (ch & 0X7))) == 0)
	    return 1;
	z->c--;
    } while (repeat);
    return 0;
}

static int slice_check(struct SN_env * z) {

    if (z->bra < 0 ||
        z->bra > z->ket ||
        z->ket > z->l ||
        z->p == NULL ||
        z->l > SIZE(z->p)) /* this line could be removed */
    {
#if 0
        fprintf(stderr, "faulty slice operation:\n");
        debug(z, -1, 0);
#endif
        return -1;
    }
    return 0;
}

extern int slice_from_s(struct SN_env * z, int s_size, const symbol * s) {
    if (slice_check(z)) return -1;
    return replace_s(z, z->bra, z->ket, s_size, s, NULL);
}

extern int slice_from_v(struct SN_env * z, const symbol * p) {
    return slice_from_s(z, SIZE(p), p);
}

extern int slice_del(struct SN_env * z) {
    return slice_from_s(z, 0, 0);
}

extern int insert_s(struct SN_env * z, int bra, int ket, int s_size, const symbol * s) {
    int adjustment;
    if (replace_s(z, bra, ket, s_size, s, &adjustment))
        return -1;
    if (bra <= z->bra) z->bra += adjustment;
    if (bra <= z->ket) z->ket += adjustment;
    return 0;
}


static int r_LONG(struct SN_env * z) {
    if (!(find_among_b(z, a_5, 7))) return 0; /* among, line 91 */
    return 1;
}

static int r_exception2(struct SN_env * z) {
    z->ket = z->c; /* [, line 158 */
    if (z->c - 5 <= z->lb || (z->p[z->c - 1] != 100 && z->p[z->c - 1] != 103)) return 0;
    if (!(find_among_b(z, a_9, 8))) return 0; /* substring, line 158 */
    z->bra = z->c; /* ], line 158 */
    if (z->c > z->lb) return 0; /* atlimit, line 158 */
    return 1;
}

extern int eq_s(struct SN_env * z, int s_size, const symbol * s) {
    if (z->l - z->c < s_size || memcmp(z->p + z->c, s, s_size * sizeof(symbol)) != 0) return 0;
    z->c += s_size; return 1;
}

extern int eq_s_b(struct SN_env * z, int s_size, const symbol * s) {
    if (z->c - z->lb < s_size || memcmp(z->p + z->c - s_size, s, s_size * sizeof(symbol)) != 0) return 0;
    z->c -= s_size; return 1;
}

extern int eq_v(struct SN_env * z, const symbol * p) {
    return eq_s(z, SIZE(p), p);
}

extern int eq_v_b(struct SN_env * z, const symbol * p) {
    return eq_s_b(z, SIZE(p), p);
}

static int r_R2(struct SN_env * z) {
    if (!(z->I[1] <= z->c)) return 0;
    return 1;
}

static int r_shortv(struct SN_env * z) {
    {   int m1 = z->l - z->c; (void)m1; /* or, line 51 */
        if (out_grouping_b_U(z, g_v_WXY, 89, 121, 0)) goto lab1;
        if (in_grouping_b_U(z, g_v, 97, 121, 0)) goto lab1;
        if (out_grouping_b_U(z, g_v, 97, 121, 0)) goto lab1;
        goto lab0;
    lab1:
        z->c = z->l - m1;
        if (out_grouping_b_U(z, g_v, 97, 121, 0)) return 0;
        if (in_grouping_b_U(z, g_v, 97, 121, 0)) return 0;
        if (z->c > z->lb) return 0; /* atlimit, line 52 */
    }
lab0:
    return 1;
}

extern symbol * slice_to(struct SN_env * z, symbol * p) {
    if (slice_check(z)) {
        lose_s(p);
        return NULL;
    }
    {
        int len = z->ket - z->bra;
        if (CAPACITY(p) < len) {
            p = increase_size(p, len);
            if (p == NULL)
                return NULL;
        }
        memmove(p, z->p + z->bra, len * sizeof(symbol));
        SET_SIZE(p, len);
    }
    return p;
}

static int r_mark_cAsInA(struct SN_env * z) {
    if (z->c - 5 <= z->lb || (z->p[z->c - 1] != 97 && z->p[z->c - 1] != 101)) return 0;
    if (!(find_among_b(z, a_19, 2))) return 0; /* among, line 283 */
    return 1;
}

static int r_mark_lArI(struct SN_env * z) {
    if (z->c - 3 <= z->lb || (z->p[z->c - 1] != 105 && z->p[z->c - 1] != 177)) return 0;
    if (!(find_among_b(z, a_1, 2))) return 0; /* among, line 179 */
    return 1;
}