Plan *
ts_prepare_sort_from_pathkeys(Plan *lefttree, List *pathkeys, Relids relids,
							  const AttrNumber *reqColIdx, bool adjust_tlist_in_place,
							  int *p_numsortkeys, AttrNumber **p_sortColIdx, Oid **p_sortOperators,
							  Oid **p_collations, bool **p_nullsFirst)
{
	List *tlist = lefttree->targetlist;
	ListCell *i;
	int numsortkeys;
	AttrNumber *sortColIdx;
	Oid *sortOperators;
	Oid *collations;
	bool *nullsFirst;

	/*
	 * We will need at most list_length(pathkeys) sort columns; possibly less
	 */
	numsortkeys = list_length(pathkeys);
	sortColIdx = (AttrNumber *) palloc(numsortkeys * sizeof(AttrNumber));
	sortOperators = (Oid *) palloc(numsortkeys * sizeof(Oid));
	collations = (Oid *) palloc(numsortkeys * sizeof(Oid));
	nullsFirst = (bool *) palloc(numsortkeys * sizeof(bool));

	numsortkeys = 0;

	foreach (i, pathkeys)
	{
		PathKey *pathkey = (PathKey *) lfirst(i);
		EquivalenceClass *ec = pathkey->pk_eclass;
		EquivalenceMember *em;
		TargetEntry *tle = NULL;
		Oid pk_datatype = InvalidOid;
		Oid sortop;
		ListCell *j;

		if (ec->ec_has_volatile)
		{
			/*
			 * If the pathkey's EquivalenceClass is volatile, then it must
			 * have come from an ORDER BY clause, and we have to match it to
			 * that same targetlist entry.
			 */
			if (ec->ec_sortref == 0) /* can't happen */
				elog(ERROR, "volatile EquivalenceClass has no sortref");
			tle = get_sortgroupref_tle(ec->ec_sortref, tlist);
			Assert(tle);
			Assert(list_length(ec->ec_members) == 1);
			pk_datatype = ((EquivalenceMember *) linitial(ec->ec_members))->em_datatype;
		}
		else if (reqColIdx != NULL)
		{
			/*
			 * If we are given a sort column number to match, only consider
			 * the single TLE at that position.  It's possible that there is
			 * no such TLE, in which case fall through and generate a resjunk
			 * targetentry (we assume this must have happened in the parent
			 * plan as well).  If there is a TLE but it doesn't match the
			 * pathkey's EC, we do the same, which is probably the wrong thing
			 * but we'll leave it to caller to complain about the mismatch.
			 */
			tle = get_tle_by_resno(tlist, reqColIdx[numsortkeys]);
			if (tle)
			{
				em = find_ec_member_matching_expr(ec, tle->expr, relids);
				if (em)
				{
					/* found expr at right place in tlist */
					pk_datatype = em->em_datatype;
				}
				else
					tle = NULL;
			}
		}
		else
		{
			/*
			 * Otherwise, we can sort by any non-constant expression listed in
			 * the pathkey's EquivalenceClass.  For now, we take the first
			 * tlist item found in the EC. If there's no match, we'll generate
			 * a resjunk entry using the first EC member that is an expression
			 * in the input's vars.  (The non-const restriction only matters
			 * if the EC is below_outer_join; but if it isn't, it won't
			 * contain consts anyway, else we'd have discarded the pathkey as
			 * redundant.)
			 *
			 * XXX if we have a choice, is there any way of figuring out which
			 * might be cheapest to execute?  (For example, int4lt is likely
			 * much cheaper to execute than numericlt, but both might appear
			 * in the same equivalence class...)  Not clear that we ever will
			 * have an interesting choice in practice, so it may not matter.
			 */
			foreach (j, tlist)
			{
				tle = (TargetEntry *) lfirst(j);
				em = find_ec_member_matching_expr(ec, tle->expr, relids);
				if (em)
				{
					/* found expr already in tlist */
					pk_datatype = em->em_datatype;
					break;
				}
				tle = NULL;
			}
		}

		if (!tle)
		{
			/*
			 * No matching tlist item; look for a computable expression.
			 */
			em = find_computable_ec_member(NULL, ec, tlist, relids, false);
			if (!em)
				elog(ERROR, "could not find pathkey item to sort");
			pk_datatype = em->em_datatype;

			/*
			 * Do we need to insert a Result node?
			 */
			if (!adjust_tlist_in_place && !is_projection_capable_plan(lefttree))
			{
				/* copy needed so we don't modify input's tlist below */
				tlist = copyObject(tlist);
				lefttree = inject_projection_plan(lefttree, tlist, lefttree->parallel_safe);
			}

			/* Don't bother testing is_projection_capable_plan again */
			adjust_tlist_in_place = true;

			/*
			 * Add resjunk entry to input's tlist
			 */
			tle = makeTargetEntry(copyObject(em->em_expr), list_length(tlist) + 1, NULL, true);
			tlist = lappend(tlist, tle);
			lefttree->targetlist = tlist; /* just in case NIL before */
		}

		/*
		 * Look up the correct sort operator from the PathKey's slightly
		 * abstracted representation.
		 */
		sortop = get_opfamily_member(pathkey->pk_opfamily,
									 pk_datatype,
									 pk_datatype,
									 pathkey->pk_strategy);
		if (!OidIsValid(sortop)) /* should not happen */
			elog(ERROR,
				 "missing operator %d(%u,%u) in opfamily %u",
				 pathkey->pk_strategy,
				 pk_datatype,
				 pk_datatype,
				 pathkey->pk_opfamily);

		/* Add the column to the sort arrays */
		sortColIdx[numsortkeys] = tle->resno;
		sortOperators[numsortkeys] = sortop;
		collations[numsortkeys] = ec->ec_collation;
		nullsFirst[numsortkeys] = pathkey->pk_nulls_first;
		numsortkeys++;
	}

	/* Return results */
	*p_numsortkeys = numsortkeys;
	*p_sortColIdx = sortColIdx;
	*p_sortOperators = sortOperators;
	*p_collations = collations;
	*p_nullsFirst = nullsFirst;

	return lefttree;
}

/*
 * ExecInitInsertProjection
 *		Do one-time initialization of projection data for INSERT tuples.
 *
 * INSERT queries may need a projection to filter out junk attrs in the tlist.
 *
 * This is also a convenient place to verify that the
 * output of an INSERT matches the target table.
 *
 * copied verbatim from executor/nodeModifyTable.c
 */
static void
ExecInitInsertProjection(ModifyTableState *mtstate, ResultRelInfo *resultRelInfo)
{
	ModifyTable *node = (ModifyTable *) mtstate->ps.plan;
	Plan *subplan = outerPlan(node);
	EState *estate = mtstate->ps.state;
	List *insertTargetList = NIL;
	bool need_projection = false;
	ListCell *l;

	/* Extract non-junk columns of the subplan's result tlist. */
	foreach (l, subplan->targetlist)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(l);

		if (!tle->resjunk)
			insertTargetList = lappend(insertTargetList, tle);
		else
			need_projection = true;
	}

	/*
	 * The junk-free list must produce a tuple suitable for the result
	 * relation.
	 */
	ExecCheckPlanOutput(resultRelInfo->ri_RelationDesc, insertTargetList);

	/* We'll need a slot matching the table's format. */
	resultRelInfo->ri_newTupleSlot =
		table_slot_create(resultRelInfo->ri_RelationDesc, &estate->es_tupleTable);

	/* Build ProjectionInfo if needed (it probably isn't). */
	if (need_projection)
	{
		TupleDesc relDesc = RelationGetDescr(resultRelInfo->ri_RelationDesc);

		/* need an expression context to do the projection */
		if (mtstate->ps.ps_ExprContext == NULL)
			ExecAssignExprContext(estate, &mtstate->ps);

		resultRelInfo->ri_projectNew = ExecBuildProjectionInfo(insertTargetList,
															   mtstate->ps.ps_ExprContext,
															   resultRelInfo->ri_newTupleSlot,
															   &mtstate->ps,
															   relDesc);
	}

	resultRelInfo->ri_projectNewInfoValid = true;
}

/*
 * ExecInitUpdateProjection
 *		Do one-time initialization of projection data for UPDATE tuples.
 *
 * UPDATE always needs a projection, because (1) there's always some junk
 * attrs, and (2) we may need to merge values of not-updated columns from
 * the old tuple into the final tuple.  In UPDATE, the tuple arriving from
 * the subplan contains only new values for the changed columns, plus row
 * identity info in the junk attrs.
 *
 * This is "one-time" for any given result rel, but we might touch more than
 * one result rel in the course of an inherited UPDATE, and each one needs
 * its own projection due to possible column order variation.
 *
 * This is also a convenient place to verify that the output of an UPDATE
 * matches the target table (ExecBuildUpdateProjection does that).
 *
 * copied verbatim from executor/nodeModifyTable.c
 */
static void
ExecInitUpdateProjection(ModifyTableState *mtstate, ResultRelInfo *resultRelInfo)
{
	ModifyTable *node = (ModifyTable *) mtstate->ps.plan;
	Plan *subplan = outerPlan(node);
	EState *estate = mtstate->ps.state;
	TupleDesc relDesc = RelationGetDescr(resultRelInfo->ri_RelationDesc);
	int whichrel;
	List *updateColnos;

	/*
	 * Usually, mt_lastResultIndex matches the target rel.  If it happens not
	 * to, we can get the index the hard way with an integer division.
	 */
	whichrel = mtstate->mt_lastResultIndex;
	if (resultRelInfo != mtstate->resultRelInfo + whichrel)
	{
		whichrel = resultRelInfo - mtstate->resultRelInfo;
		Assert(whichrel >= 0 && whichrel < mtstate->mt_nrels);
	}

	updateColnos = (List *) list_nth(node->updateColnosLists, whichrel);

	/*
	 * For UPDATE, we use the old tuple to fill up missing values in the tuple
	 * produced by the subplan to get the new tuple.  We need two slots, both
	 * matching the table's desired format.
	 */
	resultRelInfo->ri_oldTupleSlot =
		table_slot_create(resultRelInfo->ri_RelationDesc, &estate->es_tupleTable);
	resultRelInfo->ri_newTupleSlot =
		table_slot_create(resultRelInfo->ri_RelationDesc, &estate->es_tupleTable);

	/* need an expression context to do the projection */
	if (mtstate->ps.ps_ExprContext == NULL)
		ExecAssignExprContext(estate, &mtstate->ps);

	resultRelInfo->ri_projectNew = ExecBuildUpdateProjection(subplan->targetlist,
															 false, /* subplan did the evaluation */
															 updateColnos,
															 relDesc,
															 mtstate->ps.ps_ExprContext,
															 resultRelInfo->ri_newTupleSlot,
															 &mtstate->ps);

	resultRelInfo->ri_projectNewInfoValid = true;
}

/* copied from allpaths.c */
static void
set_rel_consider_parallel(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	/*
	 * The flag has previously been initialized to false, so we can just
	 * return if it becomes clear that we can't safely set it.
	 */
	Assert(!rel->consider_parallel);

	/* Don't call this if parallelism is disallowed for the entire query. */
	Assert(root->glob->parallelModeOK);

	/* This should only be called for baserels and appendrel children. */
	Assert(IS_SIMPLE_REL(rel));

	/* Assorted checks based on rtekind. */
	switch (rte->rtekind)
	{
		case RTE_RELATION:

			/*
			 * Currently, parallel workers can't access the leader's temporary
			 * tables.  We could possibly relax this if the wrote all of its
			 * local buffers at the start of the query and made no changes
			 * thereafter (maybe we could allow hint bit changes), and if we
			 * taught the workers to read them.  Writing a large number of
			 * temporary buffers could be expensive, though, and we don't have
			 * the rest of the necessary infrastructure right now anyway.  So
			 * for now, bail out if we see a temporary table.
			 */
			if (get_rel_persistence(rte->relid) == RELPERSISTENCE_TEMP)
				return;

			/*
			 * Table sampling can be pushed down to workers if the sample
			 * function and its arguments are safe.
			 */
			if (rte->tablesample != NULL)
			{
				char proparallel = func_parallel(rte->tablesample->tsmhandler);

				if (proparallel != PROPARALLEL_SAFE)
					return;
				if (!is_parallel_safe(root, (Node *) rte->tablesample->args))
					return;
			}

			/*
			 * Ask FDWs whether they can support performing a ForeignScan
			 * within a worker.  Most often, the answer will be no.  For
			 * example, if the nature of the FDW is such that it opens a TCP
			 * connection with a remote server, each parallel worker would end
			 * up with a separate connection, and these connections might not
			 * be appropriately coordinated between workers and the leader.
			 */
			if (rte->relkind == RELKIND_FOREIGN_TABLE)
			{
				Assert(rel->fdwroutine);
				if (!rel->fdwroutine->IsForeignScanParallelSafe)
					return;
				if (!rel->fdwroutine->IsForeignScanParallelSafe(root, rel, rte))
					return;
			}

			/*
			 * There are additional considerations for appendrels, which we'll
			 * deal with in set_append_rel_size and set_append_rel_pathlist.
			 * For now, just set consider_parallel based on the rel's own
			 * quals and targetlist.
			 */
			break;

		case RTE_SUBQUERY:

			/*
			 * There's no intrinsic problem with scanning a subquery-in-FROM
			 * (as distinct from a SubPlan or InitPlan) in a parallel worker.
			 * If the subquery doesn't happen to have any parallel-safe paths,
			 * then flagging it as consider_parallel won't change anything,
			 * but that's true for plain tables, too.  We must set
			 * consider_parallel based on the rel's own quals and targetlist,
			 * so that if a subquery path is parallel-safe but the quals and
			 * projection we're sticking onto it are not, we correctly mark
			 * the SubqueryScanPath as not parallel-safe.  (Note that
			 * set_subquery_pathlist() might push some of these quals down
			 * into the subquery itself, but that doesn't change anything.)
			 *
			 * We can't push sub-select containing LIMIT/OFFSET to workers as
			 * there is no guarantee that the row order will be fully
			 * deterministic, and applying LIMIT/OFFSET will lead to
			 * inconsistent results at the top-level.  (In some cases, where
			 * the result is ordered, we could relax this restriction.  But it
			 * doesn't currently seem worth expending extra effort to do so.)
			 */
			{
				Query *subquery = castNode(Query, rte->subquery);

				if (limit_needed(subquery))
					return;
			}
			break;

		case RTE_JOIN:
			/* Shouldn't happen; we're only considering baserels here. */
			Assert(false);
			return;

		case RTE_FUNCTION:
			/* Check for parallel-restricted functions. */
			if (!is_parallel_safe(root, (Node *) rte->functions))
				return;
			break;

		case RTE_TABLEFUNC:
			/* not parallel safe */
			return;

		case RTE_VALUES:
			/* Check for parallel-restricted functions. */
			if (!is_parallel_safe(root, (Node *) rte->values_lists))
				return;
			break;

		case RTE_CTE:

			/*
			 * CTE tuplestores aren't shared among parallel workers, so we
			 * force all CTE scans to happen in the leader.  Also, populating
			 * the CTE would require executing a subplan that's not available
			 * in the worker, might be parallel-restricted, and must get
			 * executed only once.
			 */
			return;

		case RTE_NAMEDTUPLESTORE:

			/*
			 * tuplestore cannot be shared, at least without more
			 * infrastructure to support that.
			 */
			return;
		case RTE_RESULT:
			/* RESULT RTEs, in themselves, are no problem. */
			break;
	}

	/*
	 * If there's anything in baserestrictinfo that's parallel-restricted, we
	 * give up on parallelizing access to this relation.  We could consider
	 * instead postponing application of the restricted quals until we're
	 * above all the parallelism in the plan tree, but it's not clear that
	 * that would be a win in very many cases, and it might be tricky to make
	 * outer join clauses work correctly.  It would likely break equivalence
	 * classes, too.
	 */
	if (!is_parallel_safe(root, (Node *) rel->baserestrictinfo))
		return;

	/*
	 * Likewise, if the relation's outputs are not parallel-safe, give up.
	 * (Usually, they're just Vars, but sometimes they're not.)
	 */
	if (!is_parallel_safe(root, (Node *) rel->reltarget->exprs))
		return;

	/* We have a winner. */
	rel->consider_parallel = true;
}

void
ts_catalog_index_insert(ResultRelInfo *indstate, HeapTuple heapTuple)
{
	int i;
	int numIndexes;
	RelationPtr relationDescs;
	Relation heapRelation;
	TupleTableSlot *slot;
	IndexInfo **indexInfoArray;
	Datum values[INDEX_MAX_KEYS];
	bool isnull[INDEX_MAX_KEYS];

	/*
	 * HOT update does not require index inserts. But with asserts enabled we
	 * want to check that it'd be legal to currently insert into the
	 * table/index.
	 */
#ifndef USE_ASSERT_CHECKING
	if (HeapTupleIsHeapOnly(heapTuple))
		return;
#endif

	/*
	 * Get information from the state structure.  Fall out if nothing to do.
	 */
	numIndexes = indstate->ri_NumIndices;
	if (numIndexes == 0)
		return;
	relationDescs = indstate->ri_IndexRelationDescs;
	indexInfoArray = indstate->ri_IndexRelationInfo;
	heapRelation = indstate->ri_RelationDesc;

	/* Need a slot to hold the tuple being examined */
	slot = MakeSingleTupleTableSlot(RelationGetDescr(heapRelation), &TTSOpsHeapTuple);
	ExecStoreHeapTuple(heapTuple, slot, false);

	/*
	 * for each index, form and insert the index tuple
	 */
	for (i = 0; i < numIndexes; i++)
	{
		IndexInfo *indexInfo;
		Relation index;

		indexInfo = indexInfoArray[i];
		index = relationDescs[i];

		/* If the index is marked as read-only, ignore it */
		if (!indexInfo->ii_ReadyForInserts)
			continue;

		/*
		 * Expressional and partial indexes on system catalogs are not
		 * supported, nor exclusion constraints, nor deferred uniqueness
		 */
		Assert(indexInfo->ii_Expressions == NIL);
		Assert(indexInfo->ii_Predicate == NIL);
		Assert(indexInfo->ii_ExclusionOps == NULL);
		Assert(index->rd_index->indimmediate);
		Assert(indexInfo->ii_NumIndexKeyAttrs != 0);

		/* see earlier check above */
#ifdef USE_ASSERT_CHECKING
		if (HeapTupleIsHeapOnly(heapTuple))
		{
			Assert(!ReindexIsProcessingIndex(RelationGetRelid(index)));
			continue;
		}
#endif /* USE_ASSERT_CHECKING */

		/*
		 * FormIndexDatum fills in its values and isnull parameters with the
		 * appropriate values for the column(s) of the index.
		 */
		FormIndexDatum(indexInfo,
					   slot,
					   NULL, /* no expression eval to do */
					   values,
					   isnull);

		/*
		 * The index AM does the rest.
		 */
		index_insert(index,				   /* index relation */
					 values,			   /* array of index Datums */
					 isnull,			   /* is-null flags */
					 &(heapTuple->t_self), /* tid of heap tuple */
					 heapRelation,
					 index->rd_index->indisunique ? UNIQUE_CHECK_YES : UNIQUE_CHECK_NO,
#if PG14_GE
					 false,
#endif
					 indexInfo);
	}

	ExecDropSingleTupleTableSlot(slot);
}

/*
 * ExecCheckTupleVisible -- verify tuple is visible
 *
 * It would not be consistent with guarantees of the higher isolation levels to
 * proceed with avoiding insertion (taking speculative insertion's alternative
 * path) on the basis of another tuple that is not visible to MVCC snapshot.
 * Check for the need to raise a serialization failure, and do so as necessary.
 *
 * copied verbatim from executor/nodeModifyTable.c
 */
static void
ExecCheckTupleVisible(EState *estate, Relation rel, TupleTableSlot *slot)
{
	if (!IsolationUsesXactSnapshot())
		return;

	if (!table_tuple_satisfies_snapshot(rel, slot, estate->es_snapshot))
	{
		Datum xminDatum;
		TransactionId xmin;
		bool isnull;

		xminDatum = slot_getsysattr(slot, MinTransactionIdAttributeNumber, &isnull);
		Assert(!isnull);
		xmin = DatumGetTransactionId(xminDatum);

		/*
		 * We should not raise a serialization failure if the conflict is
		 * against a tuple inserted by our own transaction, even if it's not
		 * visible to our snapshot.  (This would happen, for example, if
		 * conflicting keys are proposed for insertion in a single command.)
		 */
		if (!TransactionIdIsCurrentTransactionId(xmin))
			ereport(ERROR,
					(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
					 errmsg("could not serialize access due to concurrent update")));
	}
}

/*
 * ExecCheckTIDVisible -- convenience variant of ExecCheckTupleVisible()
 *
 * copied verbatim from executor/nodeModifyTable.c
 */
static void
ExecCheckTIDVisible(EState *estate, ResultRelInfo *relinfo, ItemPointer tid,
					TupleTableSlot *tempSlot)
{
	Relation rel = relinfo->ri_RelationDesc;

	/* Redundantly check isolation level */
	if (!IsolationUsesXactSnapshot())
		return;

	if (!table_tuple_fetch_row_version(rel, tid, SnapshotAny, tempSlot))
		elog(ERROR, "failed to fetch conflicting tuple for ON CONFLICT");
	ExecCheckTupleVisible(estate, rel, tempSlot);
	ExecClearTuple(tempSlot);
}

static EquivalenceMember *
find_computable_ec_member(PlannerInfo *root, EquivalenceClass *ec, List *exprs, Relids relids,
						  bool require_parallel_safe)
{
	ListCell *lc;

	foreach (lc, ec->ec_members)
	{
		EquivalenceMember *em = (EquivalenceMember *) lfirst(lc);
		List *exprvars;
		ListCell *lc2;

		/*
		 * We shouldn't be trying to sort by an equivalence class that
		 * contains a constant, so no need to consider such cases any further.
		 */
		if (em->em_is_const)
			continue;

		/*
		 * Ignore child members unless they belong to the requested rel.
		 */
		if (em->em_is_child && !bms_is_subset(em->em_relids, relids))
			continue;

		/*
		 * Match if all Vars and quasi-Vars are available in "exprs".
		 */
		exprvars = pull_var_clause((Node *) em->em_expr,
								   PVC_INCLUDE_AGGREGATES | PVC_INCLUDE_WINDOWFUNCS |
									   PVC_INCLUDE_PLACEHOLDERS);
		foreach (lc2, exprvars)
		{
			if (!is_exprlist_member(lfirst(lc2), exprs))
				break;
		}
		list_free(exprvars);
		if (lc2)
			continue; /* we hit a non-available Var */

		/*
		 * If requested, reject expressions that are not parallel-safe.  We
		 * check this last because it's a rather expensive test.
		 */
		if (require_parallel_safe && !is_parallel_safe(root, (Node *) em->em_expr))
			continue;

		return em; /* found usable expression */
	}

	return NULL;
}

PathKey *
ts_make_pathkey_from_sortinfo(PlannerInfo *root, Expr *expr, Relids nullable_relids, Oid opfamily,
							  Oid opcintype, Oid collation, bool reverse_sort, bool nulls_first,
							  Index sortref, Relids rel, bool create_it)
{
	int16 strategy;
	Oid equality_op;
	List *opfamilies;
	EquivalenceClass *eclass;

	strategy = reverse_sort ? BTGreaterStrategyNumber : BTLessStrategyNumber;

	/*
	 * EquivalenceClasses need to contain opfamily lists based on the family
	 * membership of mergejoinable equality operators, which could belong to
	 * more than one opfamily.  So we have to look up the opfamily's equality
	 * operator and get its membership.
	 */
	equality_op = get_opfamily_member(opfamily, opcintype, opcintype, BTEqualStrategyNumber);
	if (!OidIsValid(equality_op)) /* shouldn't happen */
		elog(ERROR,
			 "missing operator %d(%u,%u) in opfamily %u",
			 BTEqualStrategyNumber,
			 opcintype,
			 opcintype,
			 opfamily);
	opfamilies = get_mergejoin_opfamilies(equality_op);
	if (!opfamilies) /* certainly should find some */
		elog(ERROR, "could not find opfamilies for equality operator %u", equality_op);

	/* Now find or (optionally) create a matching EquivalenceClass */
	eclass = get_eclass_for_sort_expr(root,
									  expr,
									  nullable_relids,
									  opfamilies,
									  opcintype,
									  collation,
									  sortref,
									  rel,
									  create_it);

	/* Fail if no EC and !create_it */
	if (!eclass)
		return NULL;

	/* And finally we can find or create a PathKey node */
	return make_canonical_pathkey(root, eclass, opfamily, strategy, nulls_first);
}

static EquivalenceMember *
find_ec_member_matching_expr(EquivalenceClass *ec, Expr *expr, Relids relids)
{
	ListCell *lc;

	/* We ignore binary-compatible relabeling on both ends */
	while (expr && IsA(expr, RelabelType))
		expr = ((RelabelType *) expr)->arg;

	foreach (lc, ec->ec_members)
	{
		EquivalenceMember *em = (EquivalenceMember *) lfirst(lc);
		Expr *emexpr;

		/*
		 * We shouldn't be trying to sort by an equivalence class that
		 * contains a constant, so no need to consider such cases any further.
		 */
		if (em->em_is_const)
			continue;

		/*
		 * Ignore child members unless they belong to the requested rel.
		 */
		if (em->em_is_child && !bms_is_subset(em->em_relids, relids))
			continue;

		/*
		 * Match if same expression (after stripping relabel).
		 */
		emexpr = em->em_expr;
		while (emexpr && IsA(emexpr, RelabelType))
			emexpr = ((RelabelType *) emexpr)->arg;

		if (equal(emexpr, expr))
			return em;
	}

	return NULL;
}

List *
ts_build_path_tlist(PlannerInfo *root, Path *path)
{
	List *tlist = NIL;
	Index *sortgrouprefs = path->pathtarget->sortgrouprefs;
	int resno = 1;
	ListCell *v;

	foreach (v, path->pathtarget->exprs)
	{
		Node *node = (Node *) lfirst(v);
		TargetEntry *tle;

		/*
		 * If it's a parameterized path, there might be lateral references in
		 * the tlist, which need to be replaced with Params.  There's no need
		 * to remake the TargetEntry nodes, so apply this to each list item
		 * separately.
		 */
		if (path->param_info)
			node = replace_nestloop_params(root, node);

		tle = makeTargetEntry((Expr *) node, resno, NULL, false);
		if (sortgrouprefs)
			tle->ressortgroupref = sortgrouprefs[resno - 1];

		tlist = lappend(tlist, tle);
		resno++;
	}
	return tlist;
}

/*
 * ExecGetInsertNewTuple
 *		This prepares a "new" tuple ready to be inserted into given result
 *		relation, by removing any junk columns of the plan's output tuple
 *		and (if necessary) coercing the tuple to the right tuple format.
 *
 * copied verbatim from executor/nodeModifyTable.c
 */
static TupleTableSlot *
ExecGetInsertNewTuple(ResultRelInfo *relinfo, TupleTableSlot *planSlot)
{
	ProjectionInfo *newProj = relinfo->ri_projectNew;
	ExprContext *econtext;

	/*
	 * If there's no projection to be done, just make sure the slot is of the
	 * right type for the target rel.  If the planSlot is the right type we
	 * can use it as-is, else copy the data into ri_newTupleSlot.
	 */
	if (newProj == NULL)
	{
		if (relinfo->ri_newTupleSlot->tts_ops != planSlot->tts_ops)
		{
			ExecCopySlot(relinfo->ri_newTupleSlot, planSlot);
			return relinfo->ri_newTupleSlot;
		}
		else
			return planSlot;
	}

	/*
	 * Else project; since the projection output slot is ri_newTupleSlot, this
	 * will also fix any slot-type problem.
	 *
	 * Note: currently, this is dead code, because INSERT cases don't receive
	 * any junk columns so there's never a projection to be done.
	 */
	econtext = newProj->pi_exprContext;
	econtext->ecxt_outertuple = planSlot;
	return ExecProject(newProj);
}

/* copied from allpaths.c */
static void
set_tablesample_rel_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	Relids required_outer;
	Path *path;

	/*
	 * We don't support pushing join clauses into the quals of a samplescan,
	 * but it could still have required parameterization due to LATERAL refs
	 * in its tlist or TABLESAMPLE arguments.
	 */
	required_outer = rel->lateral_relids;

	/* Consider sampled scan */
	path = create_samplescan_path(root, rel, required_outer);

	/*
	 * If the sampling method does not support repeatable scans, we must avoid
	 * plans that would scan the rel multiple times.  Ideally, we'd simply
	 * avoid putting the rel on the inside of a nestloop join; but adding such
	 * a consideration to the planner seems like a great deal of complication
	 * to support an uncommon usage of second-rate sampling methods.  Instead,
	 * if there is a risk that the query might perform an unsafe join, just
	 * wrap the SampleScan in a Materialize node.  We can check for joins by
	 * counting the membership of all_baserels (note that this correctly
	 * counts inheritance trees as single rels).  If we're inside a subquery,
	 * we can't easily check whether a join might occur in the outer query, so
	 * just assume one is possible.
	 *
	 * GetTsmRoutine is relatively expensive compared to the other tests here,
	 * so check repeatable_across_scans last, even though that's a bit odd.
	 */
	if ((root->query_level > 1 || bms_membership(root->all_baserels) != BMS_SINGLETON) &&
		!(GetTsmRoutine(rte->tablesample->tsmhandler)->repeatable_across_scans))
	{
		path = (Path *) create_material_path(rel, path);
	}

	add_path(rel, path);

	/* For the moment, at least, there are no other paths to consider */
}