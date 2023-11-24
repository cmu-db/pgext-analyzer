Found a 61 line (253 tokens) duplication in the following files: 
Starting at line 20 of /home/abigalek/pgext-cli/pgextworkdir/pg-strom/src/gist.c
Starting at line 2614 of /home/abigalek/pgext-cli/postgresql-15.3/src/backend/optimizer/path/indxpath.c

static IndexClause *
get_index_clause_from_support(PlannerInfo *root,
                              RestrictInfo *rinfo,
                              Oid funcid,
                              int indexarg,
                              int indexcol,
                              IndexOptInfo *index)
{
	Oid			prosupport = get_func_support(funcid);
	SupportRequestIndexCondition req;
	List	   *sresult;

	if (!OidIsValid(prosupport))
		return NULL;

	req.type = T_SupportRequestIndexCondition;
	req.root = root;
	req.funcid = funcid;
	req.node = (Node *) rinfo->clause;
	req.indexarg = indexarg;
	req.index = index;
	req.indexcol = indexcol;
	req.opfamily = index->opfamily[indexcol];
	req.indexcollation = index->indexcollations[indexcol];
	req.lossy = true;		/* default assumption */

	sresult = (List *)
		DatumGetPointer(OidFunctionCall1(prosupport,
										 PointerGetDatum(&req)));
	if (sresult != NIL)
	{
		IndexClause *iclause = makeNode(IndexClause);
		List	   *indexquals = NIL;
		ListCell   *lc;

		/*
		 * The support function API says it should just give back bare
		 * clauses, so here we must wrap each one in a RestrictInfo.
		 */
		foreach(lc, sresult)
		{
			Expr	   *clause = (Expr *) lfirst(lc);

			indexquals = lappend(indexquals,
								 make_simple_restrictinfo(root, clause));
		}
		iclause->rinfo = rinfo;
		iclause->indexquals = indexquals;
		iclause->lossy = req.lossy;
		iclause->indexcol = indexcol;
		iclause->indexcols = NIL;

		return iclause;
	}
	return NULL;
}

static void
j2date(int jd, int *year, int *month, int *day)
{
	unsigned int julian;
	unsigned int quad;
	unsigned int extra;
	int		y;

	julian = jd;
	julian += 32044;
	quad = julian / 146097;
	extra = (julian - quad * 146097) * 4 + 3;
	julian += 60 + quad * 3 + extra / 146097;
	quad = julian / 1461;
	julian -= quad * 1461;
	y = julian * 4 / 1461;
	julian = ((y != 0) ? ((julian + 305) % 365) : ((julian + 306) % 366)) + 123;
	y += quad * 4;
	*year = y - 4800;
	quad = julian * 2141 / 65536;
	*day = julian - 7834 * quad / 256;
	*month = (quad + 10) % MONTHS_PER_YEAR + 1;
}

/*
 * check_null_keys from access/brin/brin.c
 */
static bool
check_null_keys(BrinValues *bval, ScanKey *nullkeys, int nnullkeys)
{
	int		keyno;

	/*
	 * First check if there are any IS [NOT] NULL scan keys, and if we're
	 * violating them.
	 */
	for (keyno = 0; keyno < nnullkeys; keyno++)
	{
		ScanKey		key = nullkeys[keyno];

		Assert(key->sk_attno == bval->bv_attno);

		/* Handle only IS NULL/IS NOT NULL tests */
		if (!(key->sk_flags & SK_ISNULL))
			continue;

		if (key->sk_flags & SK_SEARCHNULL)
		{
			/* IS NULL scan key, but range has no NULLs */
			if (!bval->bv_allnulls && !bval->bv_hasnulls)
				return false;
		}
		else if (key->sk_flags & SK_SEARCHNOTNULL)
		{
			/*
			 * For IS NOT NULL, we can only skip ranges that are known to have
			 * only nulls.
			 */
			if (bval->bv_allnulls)
				return false;
		}
		else
		{
			/*
			 * Neither IS NULL nor IS NOT NULL was used; assume all indexable
			 * operators are strict and thus return false with NULL value in
			 * the scan key.
			 */
			return false;
		}
	}
	return true;
}