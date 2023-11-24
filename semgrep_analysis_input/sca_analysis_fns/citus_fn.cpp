static void
set_join_column_names(deparse_namespace *dpns, RangeTblEntry *rte,
					  deparse_columns *colinfo)
{
	deparse_columns *leftcolinfo;
	deparse_columns *rightcolinfo;
	bool		changed_any;
	int			noldcolumns;
	int			nnewcolumns;
	Bitmapset  *leftmerged = NULL;
	Bitmapset  *rightmerged = NULL;
	int			i;
	int			j;
	int			ic;
	int			jc;

	/* Look up the previously-filled-in child deparse_columns structs */
	leftcolinfo = deparse_columns_fetch(colinfo->leftrti, dpns);
	rightcolinfo = deparse_columns_fetch(colinfo->rightrti, dpns);

	/*
	 * Ensure colinfo->colnames has a slot for each column.  (It could be long
	 * enough already, if we pushed down a name for the last column.)  Note:
	 * it's possible that one or both inputs now have more columns than there
	 * were when the query was parsed, but we'll deal with that below.  We
	 * only need entries in colnames for pre-existing columns.
	 */
	noldcolumns = list_length(rte->eref->colnames);
	expand_colnames_array_to(colinfo, noldcolumns);
	Assert(colinfo->num_cols == noldcolumns);

	/*
	 * Scan the join output columns, select an alias for each one, and store
	 * it in colinfo->colnames.  If there are USING columns, set_using_names()
	 * already selected their names, so we can start the loop at the first
	 * non-merged column.
	 */
	changed_any = false;
	for (i = list_length(colinfo->usingNames); i < noldcolumns; i++)
	{
		char	   *colname = colinfo->colnames[i];
		char	   *real_colname;

		/* Join column must refer to at least one input column */
		Assert(colinfo->leftattnos[i] != 0 || colinfo->rightattnos[i] != 0);

		/* Get the child column name */
		if (colinfo->leftattnos[i] > 0)
			real_colname = leftcolinfo->colnames[colinfo->leftattnos[i] - 1];
		else if (colinfo->rightattnos[i] > 0)
			real_colname = rightcolinfo->colnames[colinfo->rightattnos[i] - 1];
		else
		{
			/* We're joining system columns --- use eref name */
			real_colname = strVal(list_nth(rte->eref->colnames, i));
		}
			/* If child col has been dropped, no need to assign a join colname */
		if (real_colname == NULL)
		{
			colinfo->colnames[i] = NULL;
			continue;
		}

		/* In an unnamed join, just report child column names as-is */
		if (rte->alias == NULL)
		{
			colinfo->colnames[i] = real_colname;
			continue;
		}

		/* If alias already assigned, that's what to use */
		if (colname == NULL)
		{
			/* If user wrote an alias, prefer that over real column name */
			if (rte->alias && i < list_length(rte->alias->colnames))
				colname = strVal(list_nth(rte->alias->colnames, i));
			else
				colname = real_colname;

			/* Unique-ify and insert into colinfo */
			colname = make_colname_unique(colname, dpns, colinfo);

			colinfo->colnames[i] = colname;
		}

		/* Remember if any assigned aliases differ from "real" name */
		if (!changed_any && strcmp(colname, real_colname) != 0)
			changed_any = true;
	}

	/*
	 * Calculate number of columns the join would have if it were re-parsed
	 * now, and create storage for the new_colnames and is_new_col arrays.
	 *
	 * Note: colname_is_unique will be consulting new_colnames[] during the
	 * loops below, so its not-yet-filled entries must be zeroes.
	 */
	nnewcolumns = leftcolinfo->num_new_cols + rightcolinfo->num_new_cols -
		list_length(colinfo->usingNames);
	colinfo->num_new_cols = nnewcolumns;
	colinfo->new_colnames = (char **) palloc0(nnewcolumns * sizeof(char *));
	colinfo->is_new_col = (bool *) palloc0(nnewcolumns * sizeof(bool));

	/*
	 * Generating the new_colnames array is a bit tricky since any new columns
	 * added since parse time must be inserted in the right places.  This code
	 * must match the parser, which will order a join's columns as merged
	 * columns first (in USING-clause order), then non-merged columns from the
	 * left input (in attnum order), then non-merged columns from the right
	 * input (ditto).  If one of the inputs is itself a join, its columns will
	 * be ordered according to the same rule, which means newly-added columns
	 * might not be at the end.  We can figure out what's what by consulting
	 * the leftattnos and rightattnos arrays plus the input is_new_col arrays.
	 *
	 * In these loops, i indexes leftattnos/rightattnos (so it's join varattno
	 * less one), j indexes new_colnames/is_new_col, and ic/jc have similar
	 * meanings for the current child RTE.
	 */

	/* Handle merged columns; they are first and can't be new */
	i = j = 0;
	while (i < noldcolumns &&
		   colinfo->leftattnos[i] != 0 &&
		   colinfo->rightattnos[i] != 0)
	{
		/* column name is already determined and known unique */
		colinfo->new_colnames[j] = colinfo->colnames[i];
		colinfo->is_new_col[j] = false;

		/* build bitmapsets of child attnums of merged columns */
		if (colinfo->leftattnos[i] > 0)
			leftmerged = bms_add_member(leftmerged, colinfo->leftattnos[i]);
		if (colinfo->rightattnos[i] > 0)
			rightmerged = bms_add_member(rightmerged, colinfo->rightattnos[i]);

		i++, j++;
	}

	/* Handle non-merged left-child columns */
	ic = 0;
	for (jc = 0; jc < leftcolinfo->num_new_cols; jc++)
	{
		char	   *child_colname = leftcolinfo->new_colnames[jc];

		if (!leftcolinfo->is_new_col[jc])
		{
			/* Advance ic to next non-dropped old column of left child */
			while (ic < leftcolinfo->num_cols &&
				   leftcolinfo->colnames[ic] == NULL)
				ic++;
			Assert(ic < leftcolinfo->num_cols);
			ic++;
			/* If it is a merged column, we already processed it */
			if (bms_is_member(ic, leftmerged))
				continue;
			/* Else, advance i to the corresponding existing join column */
			while (i < colinfo->num_cols &&
				   colinfo->colnames[i] == NULL)
				i++;
			Assert(i < colinfo->num_cols);
			Assert(ic == colinfo->leftattnos[i]);
			/* Use the already-assigned name of this column */
			colinfo->new_colnames[j] = colinfo->colnames[i];
			i++;
		}
		else
		{
			/*
			 * Unique-ify the new child column name and assign, unless we're
			 * in an unnamed join, in which case just copy
			 */
			if (rte->alias != NULL)
			{
				colinfo->new_colnames[j] =
					make_colname_unique(child_colname, dpns, colinfo);
				if (!changed_any &&
					strcmp(colinfo->new_colnames[j], child_colname) != 0)
					changed_any = true;
			}
			else
				colinfo->new_colnames[j] = child_colname;
		}

		colinfo->is_new_col[j] = leftcolinfo->is_new_col[jc];
		j++;
	}

	/* Handle non-merged right-child columns in exactly the same way */
	ic = 0;
	for (jc = 0; jc < rightcolinfo->num_new_cols; jc++)
	{
		char	   *child_colname = rightcolinfo->new_colnames[jc];

		if (!rightcolinfo->is_new_col[jc])
		{
			/* Advance ic to next non-dropped old column of right child */
			while (ic < rightcolinfo->num_cols &&
				   rightcolinfo->colnames[ic] == NULL)
				ic++;
			Assert(ic < rightcolinfo->num_cols);
			ic++;
			/* If it is a merged column, we already processed it */
			if (bms_is_member(ic, rightmerged))
				continue;
			/* Else, advance i to the corresponding existing join column */
			while (i < colinfo->num_cols &&
				   colinfo->colnames[i] == NULL)
				i++;
			Assert(i < colinfo->num_cols);
			Assert(ic == colinfo->rightattnos[i]);
			/* Use the already-assigned name of this column */
			colinfo->new_colnames[j] = colinfo->colnames[i];
			i++;
		}
		else
		{
			/*
			 * Unique-ify the new child column name and assign, unless we're
			 * in an unnamed join, in which case just copy
			 */
			if (rte->alias != NULL)
			{
				colinfo->new_colnames[j] =
					make_colname_unique(child_colname, dpns, colinfo);
				if (!changed_any &&
					strcmp(colinfo->new_colnames[j], child_colname) != 0)
					changed_any = true;
			}
			else
				colinfo->new_colnames[j] = child_colname;
		}

		colinfo->is_new_col[j] = rightcolinfo->is_new_col[jc];
		j++;
	}

	/* Assert we processed the right number of columns */
#ifdef USE_ASSERT_CHECKING
	while (i < colinfo->num_cols && colinfo->colnames[i] == NULL)
		i++;
	Assert(i == colinfo->num_cols);
	Assert(j == nnewcolumns);
#endif

	/*
	 * For a named join, print column aliases if we changed any from the child
	 * names.  Unnamed joins cannot print aliases.
	 */
	if (rte->alias != NULL)
		colinfo->printaliases = changed_any;
	else
		colinfo->printaliases = false;
}

/*
 * colname_is_unique: is colname distinct from already-chosen column names?
 *
 * dpns is query-wide info, colinfo is for the column's RTE
 */
static bool
colname_is_unique(const char *colname, deparse_namespace *dpns,
				  deparse_columns *colinfo)
{
	int			i;
	ListCell   *lc;

	/* Check against already-assigned column aliases within RTE */
	for (i = 0; i < colinfo->num_cols; i++)
	{
		char	   *oldname = colinfo->colnames[i];

		if (oldname && strcmp(oldname, colname) == 0)
			return false;
	}

	/*
	 * If we're building a new_colnames array, check that too (this will be
	 * partially but not completely redundant with the previous checks)
	 */
	for (i = 0; i < colinfo->num_new_cols; i++)
	{
		char	   *oldname = colinfo->new_colnames[i];

		if (oldname && strcmp(oldname, colname) == 0)
			return false;
	}

	/* Also check against USING-column names that must be globally unique */
	foreach(lc, dpns->using_names)
	{
		char	   *oldname = (char *) lfirst(lc);

		if (strcmp(oldname, colname) == 0)
			return false;
	}

	/* Also check against names already assigned for parent-join USING cols */
	foreach(lc, colinfo->parentUsing)
	{
		char	   *oldname = (char *) lfirst(lc);

		if (strcmp(oldname, colname) == 0)
			return false;
	}

	return true;
}

/*
 * make_colname_unique: modify colname if necessary to make it unique
 *
 * dpns is query-wide info, colinfo is for the column's RTE
 */
static char *
make_colname_unique(char *colname, deparse_namespace *dpns,
					deparse_columns *colinfo)
{
	/*
	 * If the selected name isn't unique, append digits to make it so.  For a
	 * very long input name, we might have to truncate to stay within
	 * NAMEDATALEN.
	 */
	if (!colname_is_unique(colname, dpns, colinfo))
	{
		int			colnamelen = strlen(colname);
		char	   *modname = (char *) palloc(colnamelen + 16);
		int			i = 0;

		do
		{
			i++;
			for (;;)
			{
				memcpy(modname, colname, colnamelen);
				sprintf(modname + colnamelen, "_%d", i);
				if (strlen(modname) < NAMEDATALEN)
					break;
				/* drop chars from colname to keep all the digits */
				colnamelen = pg_mbcliplen(colname, colnamelen,
										  colnamelen - 1);
			}
		} while (!colname_is_unique(modname, dpns, colinfo));
		colname = modname;
	}
	return colname;
}

/*
 * expand_colnames_array_to: make colinfo->colnames at least n items long
 *
 * Any added array entries are initialized to zero.
 */
static void
expand_colnames_array_to(deparse_columns *colinfo, int n)
{
	if (n > colinfo->num_cols)
	{
		if (colinfo->colnames == NULL)
			colinfo->colnames = (char **) palloc0(n * sizeof(char *));
		else
		{
			colinfo->colnames = (char **) repalloc(colinfo->colnames,
												   n * sizeof(char *));
			memset(colinfo->colnames + colinfo->num_cols, 0,
				   (n - colinfo->num_cols) * sizeof(char *));
		}
		colinfo->num_cols = n;
	}
}

/*
 * identify_join_columns: figure out where columns of a join come from
 *
 * Fills the join-specific fields of the colinfo struct, except for
 * usingNames which is filled later.
 */
static void
identify_join_columns(JoinExpr *j, RangeTblEntry *jrte,
					  deparse_columns *colinfo)
{
	int			numjoincols;
	int			jcolno;
	int			rcolno;
	ListCell   *lc;

	/* Extract left/right child RT indexes */
	if (IsA(j->larg, RangeTblRef))
		colinfo->leftrti = ((RangeTblRef *) j->larg)->rtindex;
	else if (IsA(j->larg, JoinExpr))
		colinfo->leftrti = ((JoinExpr *) j->larg)->rtindex;
	else
		elog(ERROR, "unrecognized node type in jointree: %d",
			 (int) nodeTag(j->larg));
	if (IsA(j->rarg, RangeTblRef))
		colinfo->rightrti = ((RangeTblRef *) j->rarg)->rtindex;
	else if (IsA(j->rarg, JoinExpr))
		colinfo->rightrti = ((JoinExpr *) j->rarg)->rtindex;
	else
		elog(ERROR, "unrecognized node type in jointree: %d",
			 (int) nodeTag(j->rarg));

	/* Assert children will be processed earlier than join in second pass */
	Assert(colinfo->leftrti < j->rtindex);
	Assert(colinfo->rightrti < j->rtindex);

	/* Initialize result arrays with zeroes */
	numjoincols = list_length(jrte->joinaliasvars);
	Assert(numjoincols == list_length(jrte->eref->colnames));
	colinfo->leftattnos = (int *) palloc0(numjoincols * sizeof(int));
	colinfo->rightattnos = (int *) palloc0(numjoincols * sizeof(int));

	/*
	 * Deconstruct RTE's joinleftcols/joinrightcols into desired format.
	 * Recall that the column(s) merged due to USING are the first column(s)
	 * of the join output.  We need not do anything special while scanning
	 * joinleftcols, but while scanning joinrightcols we must distinguish
	 * merged from unmerged columns.
	 */
	jcolno = 0;
	foreach(lc, jrte->joinleftcols)
	{
		int			leftattno = lfirst_int(lc);

		colinfo->leftattnos[jcolno++] = leftattno;
	}
	rcolno = 0;
	foreach(lc, jrte->joinrightcols)
	{
		int			rightattno = lfirst_int(lc);

		if (rcolno < jrte->joinmergedcols)	/* merged column? */
			colinfo->rightattnos[rcolno] = rightattno;
		else
			colinfo->rightattnos[jcolno++] = rightattno;
		rcolno++;
	}
	Assert(jcolno == numjoincols);
}

/*
 * get_rtable_name: convenience function to get a previously assigned RTE alias
 *
 * The RTE must belong to the topmost namespace level in "context".
 */
static char *
get_rtable_name(int rtindex, deparse_context *context)
{
	deparse_namespace *dpns = (deparse_namespace *) linitial(context->namespaces);

	Assert(rtindex > 0 && rtindex <= list_length(dpns->rtable_names));
	return (char *) list_nth(dpns->rtable_names, rtindex - 1);
}

/*
 * set_deparse_plan: set up deparse_namespace to parse subexpressions
 * of a given Plan node
 *
 * This sets the plan, outer_planstate, inner_planstate, outer_tlist,
 * inner_tlist, and index_tlist fields.  Caller is responsible for adjusting
 * the ancestors list if necessary.  Note that the rtable and ctes fields do
 * not need to change when shifting attention to different plan nodes in a
 * single plan tree.
 */
static void
set_deparse_plan(deparse_namespace *dpns, Plan *plan)
{
	dpns->plan = plan;

	/*
	 * We special-case Append and MergeAppend to pretend that the first child
	 * plan is the OUTER referent; we have to interpret OUTER Vars in their
	 * tlists according to one of the children, and the first one is the most
	 * natural choice.
	 */
	if (IsA(plan, Append))
		dpns->outer_plan = linitial(((Append *) plan)->appendplans);
	else if (IsA(plan, MergeAppend))
		dpns->outer_plan = linitial(((MergeAppend *) plan)->mergeplans);
	else
		dpns->outer_plan = outerPlan(plan);

	if (dpns->outer_plan)
		dpns->outer_tlist = dpns->outer_plan->targetlist;
	else
		dpns->outer_tlist = NIL;

	/*
	 * For a SubqueryScan, pretend the subplan is INNER referent.  (We don't
	 * use OUTER because that could someday conflict with the normal meaning.)
	 * Likewise, for a CteScan, pretend the subquery's plan is INNER referent.
     * For a WorkTableScan, locate the parent RecursiveUnion plan node and use
     * that as INNER referent.
     *
     * For MERGE, make the inner tlist point to the merge source tlist, which
	 * is same as the targetlist that the ModifyTable's source plan provides.
	 * For ON CONFLICT .. UPDATE we just need the inner tlist to point to the
	 * excluded expression's tlist. (Similar to the SubqueryScan we don't want
	 * to reuse OUTER, it's used for RETURNING in some modify table cases,
	 * although not INSERT .. CONFLICT).
	 */
	if (IsA(plan, SubqueryScan))
		dpns->inner_plan = ((SubqueryScan *) plan)->subplan;
	else if (IsA(plan, CteScan))
		dpns->inner_plan = list_nth(dpns->subplans,
									((CteScan *) plan)->ctePlanId - 1);
    else if (IsA(plan, WorkTableScan))
		dpns->inner_plan = find_recursive_union(dpns,
												(WorkTableScan *) plan);
	else if (IsA(plan, ModifyTable))
		dpns->inner_plan = plan;
	else
		dpns->inner_plan = innerPlan(plan);

	if (IsA(plan, ModifyTable))
	{
		if (((ModifyTable *) plan)->operation == CMD_MERGE)
			dpns->inner_tlist = dpns->outer_tlist;
		else
			dpns->inner_tlist = ((ModifyTable *) plan)->exclRelTlist;
	}
	else if (dpns->inner_plan)
		dpns->inner_tlist = dpns->inner_plan->targetlist;
	else
		dpns->inner_tlist = NIL;

	/* Set up referent for INDEX_VAR Vars, if needed */
	if (IsA(plan, IndexOnlyScan))
		dpns->index_tlist = ((IndexOnlyScan *) plan)->indextlist;
	else if (IsA(plan, ForeignScan))
		dpns->index_tlist = ((ForeignScan *) plan)->fdw_scan_tlist;
	else if (IsA(plan, CustomScan))
		dpns->index_tlist = ((CustomScan *) plan)->custom_scan_tlist;
	else
		dpns->index_tlist = NIL;
}

/*
 * Locate the ancestor plan node that is the RecursiveUnion generating
 * the WorkTableScan's work table.  We can match on wtParam, since that
 * should be unique within the plan tree.
 */
static Plan *
find_recursive_union(deparse_namespace *dpns, WorkTableScan *wtscan)
{
	ListCell   *lc;

	foreach(lc, dpns->ancestors)
	{
		Plan	   *ancestor = (Plan *) lfirst(lc);

		if (IsA(ancestor, RecursiveUnion) &&
			((RecursiveUnion *) ancestor)->wtParam == wtscan->wtParam)
			return ancestor;
	}
	elog(ERROR, "could not find RecursiveUnion for WorkTableScan with wtParam %d",
		 wtscan->wtParam);
	return NULL;
}

/*
 * push_child_plan: temporarily transfer deparsing attention to a child plan
 *
 * When expanding an OUTER_VAR or INNER_VAR reference, we must adjust the
 * deparse context in case the referenced expression itself uses
 * OUTER_VAR/INNER_VAR.  We modify the top stack entry in-place to avoid
 * affecting levelsup issues (although in a Plan tree there really shouldn't
 * be any).
 *
 * Caller must provide a local deparse_namespace variable to save the
 * previous state for pop_child_plan.
 */
static void
push_child_plan(deparse_namespace *dpns, Plan *plan,
				deparse_namespace *save_dpns)
{
	/* Save state for restoration later */
	*save_dpns = *dpns;

	/* Link current plan node into ancestors list */
	dpns->ancestors = lcons(dpns->plan, dpns->ancestors);

	/* Set attention on selected child */
	set_deparse_plan(dpns, plan);
}

/*
 * pop_child_plan: undo the effects of push_child_plan
 */
static void
pop_child_plan(deparse_namespace *dpns, deparse_namespace *save_dpns)
{
	List	   *ancestors;

	/* Get rid of ancestors list cell added by push_child_plan */
	ancestors = list_delete_first(dpns->ancestors);

	/* Restore fields changed by push_child_plan */
	*dpns = *save_dpns;

	/* Make sure dpns->ancestors is right (may be unnecessary) */
	dpns->ancestors = ancestors;
}

/*
 * push_ancestor_plan: temporarily transfer deparsing attention to an
 * ancestor plan
 *
 * When expanding a Param reference, we must adjust the deparse context
 * to match the plan node that contains the expression being printed;
 * otherwise we'd fail if that expression itself contains a Param or
 * OUTER_VAR/INNER_VAR/INDEX_VAR variable.
 *
 * The target ancestor is conveniently identified by the ListCell holding it
 * in dpns->ancestors.
 *
 * Caller must provide a local deparse_namespace variable to save the
 * previous state for pop_ancestor_plan.
 */
static void
push_ancestor_plan(deparse_namespace *dpns, ListCell *ancestor_cell,
				   deparse_namespace *save_dpns)
{
	Plan	   *plan = (Plan *) lfirst(ancestor_cell);

	/* Save state for restoration later */
	*save_dpns = *dpns;

	/* Build a new ancestor list with just this node's ancestors */
	dpns->ancestors =
		list_copy_tail(dpns->ancestors,
					   list_cell_number(dpns->ancestors, ancestor_cell) + 1);

	/* Set attention on selected ancestor */
	set_deparse_plan(dpns, plan);
}

/*
 * pop_ancestor_plan: undo the effects of push_ancestor_plan
 */
static void
pop_ancestor_plan(deparse_namespace *dpns, deparse_namespace *save_dpns)
{
	/* Free the ancestor list made in push_ancestor_plan */
	list_free(dpns->ancestors);

	/* Restore fields changed by push_ancestor_plan */
	*dpns = *save_dpns;
}

/*
 * set_join_column_names: select column aliases for a join RTE
 *
 * Column alias info is saved in *colinfo, which is assumed to be pre-zeroed.
 * If any colnames entries are already filled in, those override local
 * choices.  Also, names for USING columns were already chosen by
 * set_using_names().  We further expect that column alias selection has been
 * completed for both input RTEs.
 */
static void
set_join_column_names(deparse_namespace *dpns, RangeTblEntry *rte,
					  deparse_columns *colinfo)
{
	deparse_columns *leftcolinfo;
	deparse_columns *rightcolinfo;
	bool		changed_any;
	int			noldcolumns;
	int			nnewcolumns;
	Bitmapset  *leftmerged = NULL;
	Bitmapset  *rightmerged = NULL;
	int			i;
	int			j;
	int			ic;
	int			jc;

	/* Look up the previously-filled-in child deparse_columns structs */
	leftcolinfo = deparse_columns_fetch(colinfo->leftrti, dpns);
	rightcolinfo = deparse_columns_fetch(colinfo->rightrti, dpns);

	/*
	 * Ensure colinfo->colnames has a slot for each column.  (It could be long
	 * enough already, if we pushed down a name for the last column.)  Note:
	 * it's possible that one or both inputs now have more columns than there
	 * were when the query was parsed, but we'll deal with that below.  We
	 * only need entries in colnames for pre-existing columns.
	 */
	noldcolumns = list_length(rte->eref->colnames);
	expand_colnames_array_to(colinfo, noldcolumns);
	Assert(colinfo->num_cols == noldcolumns);

	/*
	 * Scan the join output columns, select an alias for each one, and store
	 * it in colinfo->colnames.  If there are USING columns, set_using_names()
	 * already selected their names, so we can start the loop at the first
	 * non-merged column.
	 */
	changed_any = false;
	for (i = list_length(colinfo->usingNames); i < noldcolumns; i++)
	{
		char	   *colname = colinfo->colnames[i];
		char	   *real_colname;

		/* Join column must refer to at least one input column */
		Assert(colinfo->leftattnos[i] != 0 || colinfo->rightattnos[i] != 0);

		/* Get the child column name */
		if (colinfo->leftattnos[i] > 0)
			real_colname = leftcolinfo->colnames[colinfo->leftattnos[i] - 1];
		else if (colinfo->rightattnos[i] > 0)
			real_colname = rightcolinfo->colnames[colinfo->rightattnos[i] - 1];
		else
		{
			/* We're joining system columns --- use eref name */
			real_colname = strVal(list_nth(rte->eref->colnames, i));
		}
			/* If child col has been dropped, no need to assign a join colname */
		if (real_colname == NULL)
		{
			colinfo->colnames[i] = NULL;
			continue;
		}

		/* In an unnamed join, just report child column names as-is */
		if (rte->alias == NULL)
		{
			colinfo->colnames[i] = real_colname;
			continue;
		}

		/* If alias already assigned, that's what to use */
		if (colname == NULL)
		{
			/* If user wrote an alias, prefer that over real column name */
			if (rte->alias && i < list_length(rte->alias->colnames))
				colname = strVal(list_nth(rte->alias->colnames, i));
			else
				colname = real_colname;

			/* Unique-ify and insert into colinfo */
			colname = make_colname_unique(colname, dpns, colinfo);

			colinfo->colnames[i] = colname;
		}

		/* Remember if any assigned aliases differ from "real" name */
		if (!changed_any && strcmp(colname, real_colname) != 0)
			changed_any = true;
	}

	/*
	 * Calculate number of columns the join would have if it were re-parsed
	 * now, and create storage for the new_colnames and is_new_col arrays.
	 *
	 * Note: colname_is_unique will be consulting new_colnames[] during the
	 * loops below, so its not-yet-filled entries must be zeroes.
	 */
	nnewcolumns = leftcolinfo->num_new_cols + rightcolinfo->num_new_cols -
		list_length(colinfo->usingNames);
	colinfo->num_new_cols = nnewcolumns;
	colinfo->new_colnames = (char **) palloc0(nnewcolumns * sizeof(char *));
	colinfo->is_new_col = (bool *) palloc0(nnewcolumns * sizeof(bool));

	/*
	 * Generating the new_colnames array is a bit tricky since any new columns
	 * added since parse time must be inserted in the right places.  This code
	 * must match the parser, which will order a join's columns as merged
	 * columns first (in USING-clause order), then non-merged columns from the
	 * left input (in attnum order), then non-merged columns from the right
	 * input (ditto).  If one of the inputs is itself a join, its columns will
	 * be ordered according to the same rule, which means newly-added columns
	 * might not be at the end.  We can figure out what's what by consulting
	 * the leftattnos and rightattnos arrays plus the input is_new_col arrays.
	 *
	 * In these loops, i indexes leftattnos/rightattnos (so it's join varattno
	 * less one), j indexes new_colnames/is_new_col, and ic/jc have similar
	 * meanings for the current child RTE.
	 */

	/* Handle merged columns; they are first and can't be new */
	i = j = 0;
	while (i < noldcolumns &&
		   colinfo->leftattnos[i] != 0 &&
		   colinfo->rightattnos[i] != 0)
	{
		/* column name is already determined and known unique */
		colinfo->new_colnames[j] = colinfo->colnames[i];
		colinfo->is_new_col[j] = false;

		/* build bitmapsets of child attnums of merged columns */
		if (colinfo->leftattnos[i] > 0)
			leftmerged = bms_add_member(leftmerged, colinfo->leftattnos[i]);
		if (colinfo->rightattnos[i] > 0)
			rightmerged = bms_add_member(rightmerged, colinfo->rightattnos[i]);

		i++, j++;
	}

	/* Handle non-merged left-child columns */
	ic = 0;
	for (jc = 0; jc < leftcolinfo->num_new_cols; jc++)
	{
		char	   *child_colname = leftcolinfo->new_colnames[jc];

		if (!leftcolinfo->is_new_col[jc])
		{
			/* Advance ic to next non-dropped old column of left child */
			while (ic < leftcolinfo->num_cols &&
				   leftcolinfo->colnames[ic] == NULL)
				ic++;
			Assert(ic < leftcolinfo->num_cols);
			ic++;
			/* If it is a merged column, we already processed it */
			if (bms_is_member(ic, leftmerged))
				continue;
			/* Else, advance i to the corresponding existing join column */
			while (i < colinfo->num_cols &&
				   colinfo->colnames[i] == NULL)
				i++;
			Assert(i < colinfo->num_cols);
			Assert(ic == colinfo->leftattnos[i]);
			/* Use the already-assigned name of this column */
			colinfo->new_colnames[j] = colinfo->colnames[i];
			i++;
		}
		else
		{
			/*
			 * Unique-ify the new child column name and assign, unless we're
			 * in an unnamed join, in which case just copy
			 */
			if (rte->alias != NULL)
			{
				colinfo->new_colnames[j] =
					make_colname_unique(child_colname, dpns, colinfo);
				if (!changed_any &&
					strcmp(colinfo->new_colnames[j], child_colname) != 0)
					changed_any = true;
			}
			else
				colinfo->new_colnames[j] = child_colname;
		}

		colinfo->is_new_col[j] = leftcolinfo->is_new_col[jc];
		j++;
	}

	/* Handle non-merged right-child columns in exactly the same way */
	ic = 0;
	for (jc = 0; jc < rightcolinfo->num_new_cols; jc++)
	{
		char	   *child_colname = rightcolinfo->new_colnames[jc];

		if (!rightcolinfo->is_new_col[jc])
		{
			/* Advance ic to next non-dropped old column of right child */
			while (ic < rightcolinfo->num_cols &&
				   rightcolinfo->colnames[ic] == NULL)
				ic++;
			Assert(ic < rightcolinfo->num_cols);
			ic++;
			/* If it is a merged column, we already processed it */
			if (bms_is_member(ic, rightmerged))
				continue;
			/* Else, advance i to the corresponding existing join column */
			while (i < colinfo->num_cols &&
				   colinfo->colnames[i] == NULL)
				i++;
			Assert(i < colinfo->num_cols);
			Assert(ic == colinfo->rightattnos[i]);
			/* Use the already-assigned name of this column */
			colinfo->new_colnames[j] = colinfo->colnames[i];
			i++;
		}
		else
		{
			/*
			 * Unique-ify the new child column name and assign, unless we're
			 * in an unnamed join, in which case just copy
			 */
			if (rte->alias != NULL)
			{
				colinfo->new_colnames[j] =
					make_colname_unique(child_colname, dpns, colinfo);
				if (!changed_any &&
					strcmp(colinfo->new_colnames[j], child_colname) != 0)
					changed_any = true;
			}
			else
				colinfo->new_colnames[j] = child_colname;
		}

		colinfo->is_new_col[j] = rightcolinfo->is_new_col[jc];
		j++;
	}

	/* Assert we processed the right number of columns */
#ifdef USE_ASSERT_CHECKING
	while (i < colinfo->num_cols && colinfo->colnames[i] == NULL)
		i++;
	Assert(i == colinfo->num_cols);
	Assert(j == nnewcolumns);
#endif

	/*
	 * For a named join, print column aliases if we changed any from the child
	 * names.  Unnamed joins cannot print aliases.
	 */
	if (rte->alias != NULL)
		colinfo->printaliases = changed_any;
	else
		colinfo->printaliases = false;
}

/*
 * colname_is_unique: is colname distinct from already-chosen column names?
 *
 * dpns is query-wide info, colinfo is for the column's RTE
 */
static bool
colname_is_unique(const char *colname, deparse_namespace *dpns,
				  deparse_columns *colinfo)
{
	int			i;
	ListCell   *lc;

	/* Check against already-assigned column aliases within RTE */
	for (i = 0; i < colinfo->num_cols; i++)
	{
		char	   *oldname = colinfo->colnames[i];

		if (oldname && strcmp(oldname, colname) == 0)
			return false;
	}

	/*
	 * If we're building a new_colnames array, check that too (this will be
	 * partially but not completely redundant with the previous checks)
	 */
	for (i = 0; i < colinfo->num_new_cols; i++)
	{
		char	   *oldname = colinfo->new_colnames[i];

		if (oldname && strcmp(oldname, colname) == 0)
			return false;
	}

	/* Also check against USING-column names that must be globally unique */
	foreach(lc, dpns->using_names)
	{
		char	   *oldname = (char *) lfirst(lc);

		if (strcmp(oldname, colname) == 0)
			return false;
	}

	/* Also check against names already assigned for parent-join USING cols */
	foreach(lc, colinfo->parentUsing)
	{
		char	   *oldname = (char *) lfirst(lc);

		if (strcmp(oldname, colname) == 0)
			return false;
	}

	return true;
}

/*
 * make_colname_unique: modify colname if necessary to make it unique
 *
 * dpns is query-wide info, colinfo is for the column's RTE
 */
static char *
make_colname_unique(char *colname, deparse_namespace *dpns,
					deparse_columns *colinfo)
{
	/*
	 * If the selected name isn't unique, append digits to make it so.  For a
	 * very long input name, we might have to truncate to stay within
	 * NAMEDATALEN.
	 */
	if (!colname_is_unique(colname, dpns, colinfo))
	{
		int			colnamelen = strlen(colname);
		char	   *modname = (char *) palloc(colnamelen + 16);
		int			i = 0;

		do
		{
			i++;
			for (;;)
			{
				memcpy(modname, colname, colnamelen);
				sprintf(modname + colnamelen, "_%d", i);
				if (strlen(modname) < NAMEDATALEN)
					break;
				/* drop chars from colname to keep all the digits */
				colnamelen = pg_mbcliplen(colname, colnamelen,
										  colnamelen - 1);
			}
		} while (!colname_is_unique(modname, dpns, colinfo));
		colname = modname;
	}
	return colname;
}

/*
 * expand_colnames_array_to: make colinfo->colnames at least n items long
 *
 * Any added array entries are initialized to zero.
 */
static void
expand_colnames_array_to(deparse_columns *colinfo, int n)
{
	if (n > colinfo->num_cols)
	{
		if (colinfo->colnames == NULL)
			colinfo->colnames = (char **) palloc0(n * sizeof(char *));
		else
		{
			colinfo->colnames = (char **) repalloc(colinfo->colnames,
												   n * sizeof(char *));
			memset(colinfo->colnames + colinfo->num_cols, 0,
				   (n - colinfo->num_cols) * sizeof(char *));
		}
		colinfo->num_cols = n;
	}
}

/*
 * identify_join_columns: figure out where columns of a join come from
 *
 * Fills the join-specific fields of the colinfo struct, except for
 * usingNames which is filled later.
 */
static void
identify_join_columns(JoinExpr *j, RangeTblEntry *jrte,
					  deparse_columns *colinfo)
{
	int			numjoincols;
	int			jcolno;
	int			rcolno;
	ListCell   *lc;

	/* Extract left/right child RT indexes */
	if (IsA(j->larg, RangeTblRef))
		colinfo->leftrti = ((RangeTblRef *) j->larg)->rtindex;
	else if (IsA(j->larg, JoinExpr))
		colinfo->leftrti = ((JoinExpr *) j->larg)->rtindex;
	else
		elog(ERROR, "unrecognized node type in jointree: %d",
			 (int) nodeTag(j->larg));
	if (IsA(j->rarg, RangeTblRef))
		colinfo->rightrti = ((RangeTblRef *) j->rarg)->rtindex;
	else if (IsA(j->rarg, JoinExpr))
		colinfo->rightrti = ((JoinExpr *) j->rarg)->rtindex;
	else
		elog(ERROR, "unrecognized node type in jointree: %d",
			 (int) nodeTag(j->rarg));

	/* Assert children will be processed earlier than join in second pass */
	Assert(colinfo->leftrti < j->rtindex);
	Assert(colinfo->rightrti < j->rtindex);

	/* Initialize result arrays with zeroes */
	numjoincols = list_length(jrte->joinaliasvars);
	Assert(numjoincols == list_length(jrte->eref->colnames));
	colinfo->leftattnos = (int *) palloc0(numjoincols * sizeof(int));
	colinfo->rightattnos = (int *) palloc0(numjoincols * sizeof(int));

	/*
	 * Deconstruct RTE's joinleftcols/joinrightcols into desired format.
	 * Recall that the column(s) merged due to USING are the first column(s)
	 * of the join output.  We need not do anything special while scanning
	 * joinleftcols, but while scanning joinrightcols we must distinguish
	 * merged from unmerged columns.
	 */
	jcolno = 0;
	foreach(lc, jrte->joinleftcols)
	{
		int			leftattno = lfirst_int(lc);

		colinfo->leftattnos[jcolno++] = leftattno;
	}
	rcolno = 0;
	foreach(lc, jrte->joinrightcols)
	{
		int			rightattno = lfirst_int(lc);

		if (rcolno < jrte->joinmergedcols)	/* merged column? */
			colinfo->rightattnos[rcolno] = rightattno;
		else
			colinfo->rightattnos[jcolno++] = rightattno;
		rcolno++;
	}
	Assert(jcolno == numjoincols);
}

/*
 * get_rtable_name: convenience function to get a previously assigned RTE alias
 *
 * The RTE must belong to the topmost namespace level in "context".
 */
static char *
get_rtable_name(int rtindex, deparse_context *context)
{
	deparse_namespace *dpns = (deparse_namespace *) linitial(context->namespaces);

	Assert(rtindex > 0 && rtindex <= list_length(dpns->rtable_names));
	return (char *) list_nth(dpns->rtable_names, rtindex - 1);
}

/*
 * set_join_column_names: select column aliases for a join RTE
 *
 * Column alias info is saved in *colinfo, which is assumed to be pre-zeroed.
 * If any colnames entries are already filled in, those override local
 * choices.  Also, names for USING columns were already chosen by
 * set_using_names().  We further expect that column alias selection has been
 * completed for both input RTEs.
 */
static void
set_join_column_names(deparse_namespace *dpns, RangeTblEntry *rte,
					  deparse_columns *colinfo)
{
	deparse_columns *leftcolinfo;
	deparse_columns *rightcolinfo;
	bool		changed_any;
	int			noldcolumns;
	int			nnewcolumns;
	Bitmapset  *leftmerged = NULL;
	Bitmapset  *rightmerged = NULL;
	int			i;
	int			j;
	int			ic;
	int			jc;

	/* Look up the previously-filled-in child deparse_columns structs */
	leftcolinfo = deparse_columns_fetch(colinfo->leftrti, dpns);
	rightcolinfo = deparse_columns_fetch(colinfo->rightrti, dpns);

	/*
	 * Ensure colinfo->colnames has a slot for each column.  (It could be long
	 * enough already, if we pushed down a name for the last column.)  Note:
	 * it's possible that one or both inputs now have more columns than there
	 * were when the query was parsed, but we'll deal with that below.  We
	 * only need entries in colnames for pre-existing columns.
	 */
	noldcolumns = list_length(rte->eref->colnames);
	expand_colnames_array_to(colinfo, noldcolumns);
	Assert(colinfo->num_cols == noldcolumns);

	/*
	 * Scan the join output columns, select an alias for each one, and store
	 * it in colinfo->colnames.  If there are USING columns, set_using_names()
	 * already selected their names, so we can start the loop at the first
	 * non-merged column.
	 */
	changed_any = false;
	for (i = list_length(colinfo->usingNames); i < noldcolumns; i++)
	{
		char	   *colname = colinfo->colnames[i];
		char	   *real_colname;

		/* Join column must refer to at least one input column */
		Assert(colinfo->leftattnos[i] != 0 || colinfo->rightattnos[i] != 0);

		/* Get the child column name */
		if (colinfo->leftattnos[i] > 0)
			real_colname = leftcolinfo->colnames[colinfo->leftattnos[i] - 1];
		else if (colinfo->rightattnos[i] > 0)
			real_colname = rightcolinfo->colnames[colinfo->rightattnos[i] - 1];
		else
		{
			/* We're joining system columns --- use eref name */
			real_colname = strVal(list_nth(rte->eref->colnames, i));
		}
			/* If child col has been dropped, no need to assign a join colname */
		if (real_colname == NULL)
		{
			colinfo->colnames[i] = NULL;
			continue;
		}

		/* In an unnamed join, just report child column names as-is */
		if (rte->alias == NULL)
		{
			colinfo->colnames[i] = real_colname;
			continue;
		}

		/* If alias already assigned, that's what to use */
		if (colname == NULL)
		{
			/* If user wrote an alias, prefer that over real column name */
			if (rte->alias && i < list_length(rte->alias->colnames))
				colname = strVal(list_nth(rte->alias->colnames, i));
			else
				colname = real_colname;

			/* Unique-ify and insert into colinfo */
			colname = make_colname_unique(colname, dpns, colinfo);

			colinfo->colnames[i] = colname;
		}

		/* Remember if any assigned aliases differ from "real" name */
		if (!changed_any && strcmp(colname, real_colname) != 0)
			changed_any = true;
	}

	/*
	 * Calculate number of columns the join would have if it were re-parsed
	 * now, and create storage for the new_colnames and is_new_col arrays.
	 *
	 * Note: colname_is_unique will be consulting new_colnames[] during the
	 * loops below, so its not-yet-filled entries must be zeroes.
	 */
	nnewcolumns = leftcolinfo->num_new_cols + rightcolinfo->num_new_cols -
		list_length(colinfo->usingNames);
	colinfo->num_new_cols = nnewcolumns;
	colinfo->new_colnames = (char **) palloc0(nnewcolumns * sizeof(char *));
	colinfo->is_new_col = (bool *) palloc0(nnewcolumns * sizeof(bool));

	/*
	 * Generating the new_colnames array is a bit tricky since any new columns
	 * added since parse time must be inserted in the right places.  This code
	 * must match the parser, which will order a join's columns as merged
	 * columns first (in USING-clause order), then non-merged columns from the
	 * left input (in attnum order), then non-merged columns from the right
	 * input (ditto).  If one of the inputs is itself a join, its columns will
	 * be ordered according to the same rule, which means newly-added columns
	 * might not be at the end.  We can figure out what's what by consulting
	 * the leftattnos and rightattnos arrays plus the input is_new_col arrays.
	 *
	 * In these loops, i indexes leftattnos/rightattnos (so it's join varattno
	 * less one), j indexes new_colnames/is_new_col, and ic/jc have similar
	 * meanings for the current child RTE.
	 */

	/* Handle merged columns; they are first and can't be new */
	i = j = 0;
	while (i < noldcolumns &&
		   colinfo->leftattnos[i] != 0 &&
		   colinfo->rightattnos[i] != 0)
	{
		/* column name is already determined and known unique */
		colinfo->new_colnames[j] = colinfo->colnames[i];
		colinfo->is_new_col[j] = false;

		/* build bitmapsets of child attnums of merged columns */
		if (colinfo->leftattnos[i] > 0)
			leftmerged = bms_add_member(leftmerged, colinfo->leftattnos[i]);
		if (colinfo->rightattnos[i] > 0)
			rightmerged = bms_add_member(rightmerged, colinfo->rightattnos[i]);

		i++, j++;
	}

	/* Handle non-merged left-child columns */
	ic = 0;
	for (jc = 0; jc < leftcolinfo->num_new_cols; jc++)
	{
		char	   *child_colname = leftcolinfo->new_colnames[jc];

		if (!leftcolinfo->is_new_col[jc])
		{
			/* Advance ic to next non-dropped old column of left child */
			while (ic < leftcolinfo->num_cols &&
				   leftcolinfo->colnames[ic] == NULL)
				ic++;
			Assert(ic < leftcolinfo->num_cols);
			ic++;
			/* If it is a merged column, we already processed it */
			if (bms_is_member(ic, leftmerged))
				continue;
			/* Else, advance i to the corresponding existing join column */
			while (i < colinfo->num_cols &&
				   colinfo->colnames[i] == NULL)
				i++;
			Assert(i < colinfo->num_cols);
			Assert(ic == colinfo->leftattnos[i]);
			/* Use the already-assigned name of this column */
			colinfo->new_colnames[j] = colinfo->colnames[i];
			i++;
		}
		else
		{
			/*
			 * Unique-ify the new child column name and assign, unless we're
			 * in an unnamed join, in which case just copy
			 */
			if (rte->alias != NULL)
			{
				colinfo->new_colnames[j] =
					make_colname_unique(child_colname, dpns, colinfo);
				if (!changed_any &&
					strcmp(colinfo->new_colnames[j], child_colname) != 0)
					changed_any = true;
			}
			else
				colinfo->new_colnames[j] = child_colname;
		}

		colinfo->is_new_col[j] = leftcolinfo->is_new_col[jc];
		j++;
	}

	/* Handle non-merged right-child columns in exactly the same way */
	ic = 0;
	for (jc = 0; jc < rightcolinfo->num_new_cols; jc++)
	{
		char	   *child_colname = rightcolinfo->new_colnames[jc];

		if (!rightcolinfo->is_new_col[jc])
		{
			/* Advance ic to next non-dropped old column of right child */
			while (ic < rightcolinfo->num_cols &&
				   rightcolinfo->colnames[ic] == NULL)
				ic++;
			Assert(ic < rightcolinfo->num_cols);
			ic++;
			/* If it is a merged column, we already processed it */
			if (bms_is_member(ic, rightmerged))
				continue;
			/* Else, advance i to the corresponding existing join column */
			while (i < colinfo->num_cols &&
				   colinfo->colnames[i] == NULL)
				i++;
			Assert(i < colinfo->num_cols);
			Assert(ic == colinfo->rightattnos[i]);
			/* Use the already-assigned name of this column */
			colinfo->new_colnames[j] = colinfo->colnames[i];
			i++;
		}
		else
		{
			/*
			 * Unique-ify the new child column name and assign, unless we're
			 * in an unnamed join, in which case just copy
			 */
			if (rte->alias != NULL)
			{
				colinfo->new_colnames[j] =
					make_colname_unique(child_colname, dpns, colinfo);
				if (!changed_any &&
					strcmp(colinfo->new_colnames[j], child_colname) != 0)
					changed_any = true;
			}
			else
				colinfo->new_colnames[j] = child_colname;
		}

		colinfo->is_new_col[j] = rightcolinfo->is_new_col[jc];
		j++;
	}

	/* Assert we processed the right number of columns */
#ifdef USE_ASSERT_CHECKING
	while (i < colinfo->num_cols && colinfo->colnames[i] == NULL)
		i++;
	Assert(i == colinfo->num_cols);
	Assert(j == nnewcolumns);
#endif

	/*
	 * For a named join, print column aliases if we changed any from the child
	 * names.  Unnamed joins cannot print aliases.
	 */
	if (rte->alias != NULL)
		colinfo->printaliases = changed_any;
	else
		colinfo->printaliases = false;
}

/*
 * colname_is_unique: is colname distinct from already-chosen column names?
 *
 * dpns is query-wide info, colinfo is for the column's RTE
 */
static bool
colname_is_unique(const char *colname, deparse_namespace *dpns,
				  deparse_columns *colinfo)
{
	int			i;
	ListCell   *lc;

	/* Check against already-assigned column aliases within RTE */
	for (i = 0; i < colinfo->num_cols; i++)
	{
		char	   *oldname = colinfo->colnames[i];

		if (oldname && strcmp(oldname, colname) == 0)
			return false;
	}

	/*
	 * If we're building a new_colnames array, check that too (this will be
	 * partially but not completely redundant with the previous checks)
	 */
	for (i = 0; i < colinfo->num_new_cols; i++)
	{
		char	   *oldname = colinfo->new_colnames[i];

		if (oldname && strcmp(oldname, colname) == 0)
			return false;
	}

	/* Also check against USING-column names that must be globally unique */
	foreach(lc, dpns->using_names)
	{
		char	   *oldname = (char *) lfirst(lc);

		if (strcmp(oldname, colname) == 0)
			return false;
	}

	/* Also check against names already assigned for parent-join USING cols */
	foreach(lc, colinfo->parentUsing)
	{
		char	   *oldname = (char *) lfirst(lc);

		if (strcmp(oldname, colname) == 0)
			return false;
	}

	return true;
}

/*
 * make_colname_unique: modify colname if necessary to make it unique
 *
 * dpns is query-wide info, colinfo is for the column's RTE
 */
static char *
make_colname_unique(char *colname, deparse_namespace *dpns,
					deparse_columns *colinfo)
{
	/*
	 * If the selected name isn't unique, append digits to make it so.  For a
	 * very long input name, we might have to truncate to stay within
	 * NAMEDATALEN.
	 */
	if (!colname_is_unique(colname, dpns, colinfo))
	{
		int			colnamelen = strlen(colname);
		char	   *modname = (char *) palloc(colnamelen + 16);
		int			i = 0;

		do
		{
			i++;
			for (;;)
			{
				/*
				 * We avoid using %.*s here because it can misbehave if the
				 * data is not valid in what libc thinks is the prevailing
				 * encoding.
				 */
				memcpy(modname, colname, colnamelen);
				sprintf(modname + colnamelen, "_%d", i);
				if (strlen(modname) < NAMEDATALEN)
					break;
				/* drop chars from colname to keep all the digits */
				colnamelen = pg_mbcliplen(colname, colnamelen,
										  colnamelen - 1);
			}
		} while (!colname_is_unique(modname, dpns, colinfo));
		colname = modname;
	}
	return colname;
}

/*
 * expand_colnames_array_to: make colinfo->colnames at least n items long
 *
 * Any added array entries are initialized to zero.
 */
static void
expand_colnames_array_to(deparse_columns *colinfo, int n)
{
	if (n > colinfo->num_cols)
	{
		if (colinfo->colnames == NULL)
			colinfo->colnames = (char **) palloc0(n * sizeof(char *));
		else
		{
			colinfo->colnames = (char **) repalloc(colinfo->colnames,
												   n * sizeof(char *));
			memset(colinfo->colnames + colinfo->num_cols, 0,
				   (n - colinfo->num_cols) * sizeof(char *));
		}
		colinfo->num_cols = n;
	}
}

/*
 * identify_join_columns: figure out where columns of a join come from
 *
 * Fills the join-specific fields of the colinfo struct, except for
 * usingNames which is filled later.
 */
static void
identify_join_columns(JoinExpr *j, RangeTblEntry *jrte,
					  deparse_columns *colinfo)
{
	int			numjoincols;
	int			jcolno;
	int			rcolno;
	ListCell   *lc;

	/* Extract left/right child RT indexes */
	if (IsA(j->larg, RangeTblRef))
		colinfo->leftrti = ((RangeTblRef *) j->larg)->rtindex;
	else if (IsA(j->larg, JoinExpr))
		colinfo->leftrti = ((JoinExpr *) j->larg)->rtindex;
	else
		elog(ERROR, "unrecognized node type in jointree: %d",
			 (int) nodeTag(j->larg));
	if (IsA(j->rarg, RangeTblRef))
		colinfo->rightrti = ((RangeTblRef *) j->rarg)->rtindex;
	else if (IsA(j->rarg, JoinExpr))
		colinfo->rightrti = ((JoinExpr *) j->rarg)->rtindex;
	else
		elog(ERROR, "unrecognized node type in jointree: %d",
			 (int) nodeTag(j->rarg));

	/* Assert children will be processed earlier than join in second pass */
	Assert(colinfo->leftrti < j->rtindex);
	Assert(colinfo->rightrti < j->rtindex);

	/* Initialize result arrays with zeroes */
	numjoincols = list_length(jrte->joinaliasvars);
	Assert(numjoincols == list_length(jrte->eref->colnames));
	colinfo->leftattnos = (int *) palloc0(numjoincols * sizeof(int));
	colinfo->rightattnos = (int *) palloc0(numjoincols * sizeof(int));

	/*
	 * Deconstruct RTE's joinleftcols/joinrightcols into desired format.
	 * Recall that the column(s) merged due to USING are the first column(s)
	 * of the join output.  We need not do anything special while scanning
	 * joinleftcols, but while scanning joinrightcols we must distinguish
	 * merged from unmerged columns.
	 */
	jcolno = 0;
	foreach(lc, jrte->joinleftcols)
	{
		int			leftattno = lfirst_int(lc);

		colinfo->leftattnos[jcolno++] = leftattno;
	}
	rcolno = 0;
	foreach(lc, jrte->joinrightcols)
	{
		int			rightattno = lfirst_int(lc);

		if (rcolno < jrte->joinmergedcols)	/* merged column? */
			colinfo->rightattnos[rcolno] = rightattno;
		else
			colinfo->rightattnos[jcolno++] = rightattno;
		rcolno++;
	}
	Assert(jcolno == numjoincols);
}

/*
 * get_rtable_name: convenience function to get a previously assigned RTE alias
 *
 * The RTE must belong to the topmost namespace level in "context".
 */
static char *
get_rtable_name(int rtindex, deparse_context *context)
{
	deparse_namespace *dpns = (deparse_namespace *) linitial(context->namespaces);

	Assert(rtindex > 0 && rtindex <= list_length(dpns->rtable_names));
	return (char *) list_nth(dpns->rtable_names, rtindex - 1);
}

static void
get_setop_query(Node *setOp, Query *query, deparse_context *context,
				TupleDesc resultDesc, bool colNamesVisible)
{
	StringInfo	buf = context->buf;
	bool		need_paren;

	/* Guard against excessively long or deeply-nested queries */
	CHECK_FOR_INTERRUPTS();
	check_stack_depth();

	if (IsA(setOp, RangeTblRef))
	{
		RangeTblRef *rtr = (RangeTblRef *) setOp;
		RangeTblEntry *rte = rt_fetch(rtr->rtindex, query->rtable);
		Query	   *subquery = rte->subquery;

		Assert(subquery != NULL);
		Assert(subquery->setOperations == NULL);
		/* Need parens if WITH, ORDER BY, FOR UPDATE, or LIMIT; see gram.y */
		need_paren = (subquery->cteList ||
					  subquery->sortClause ||
					  subquery->rowMarks ||
					  subquery->limitOffset ||
					  subquery->limitCount);
		if (need_paren)
			appendStringInfoChar(buf, '(');
		get_query_def(subquery, buf, context->namespaces, resultDesc,
		              colNamesVisible,
					  context->prettyFlags, context->wrapColumn,
					  context->indentLevel);
		if (need_paren)
			appendStringInfoChar(buf, ')');
	}
	else if (IsA(setOp, SetOperationStmt))
	{
		SetOperationStmt *op = (SetOperationStmt *) setOp;
		int			subindent;

		/*
		 * We force parens when nesting two SetOperationStmts, except when the
		 * lefthand input is another setop of the same kind.  Syntactically,
		 * we could omit parens in rather more cases, but it seems best to use
		 * parens to flag cases where the setop operator changes.  If we use
		 * parens, we also increase the indentation level for the child query.
		 *
		 * There are some cases in which parens are needed around a leaf query
		 * too, but those are more easily handled at the next level down (see
		 * code above).
		 */
		if (IsA(op->larg, SetOperationStmt))
		{
			SetOperationStmt *lop = (SetOperationStmt *) op->larg;

			if (op->op == lop->op && op->all == lop->all)
				need_paren = false;
			else
				need_paren = true;
		}
		else
			need_paren = false;

		if (need_paren)
		{
			appendStringInfoChar(buf, '(');
			subindent = PRETTYINDENT_STD;
			appendContextKeyword(context, "", subindent, 0, 0);
		}
		else
			subindent = 0;

		get_setop_query(op->larg, query, context, resultDesc, colNamesVisible);

		if (need_paren)
			appendContextKeyword(context, ") ", -subindent, 0, 0);
		else if (PRETTY_INDENT(context))
			appendContextKeyword(context, "", -subindent, 0, 0);
		else
			appendStringInfoChar(buf, ' ');

		switch (op->op)
		{
			case SETOP_UNION:
				appendStringInfoString(buf, "UNION ");
				break;
			case SETOP_INTERSECT:
				appendStringInfoString(buf, "INTERSECT ");
				break;
			case SETOP_EXCEPT:
				appendStringInfoString(buf, "EXCEPT ");
				break;
			default:
				elog(ERROR, "unrecognized set op: %d",
					 (int) op->op);
		}
		if (op->all)
			appendStringInfoString(buf, "ALL ");

		/* Always parenthesize if RHS is another setop */
		need_paren = IsA(op->rarg, SetOperationStmt);

		/*
		 * The indentation code here is deliberately a bit different from that
		 * for the lefthand input, because we want the line breaks in
		 * different places.
		 */
		if (need_paren)
		{
			appendStringInfoChar(buf, '(');
			subindent = PRETTYINDENT_STD;
		}
		else
			subindent = 0;
		appendContextKeyword(context, "", subindent, 0, 0);

		get_setop_query(op->rarg, query, context, resultDesc, false);

		if (PRETTY_INDENT(context))
			context->indentLevel -= subindent;
		if (need_paren)
			appendContextKeyword(context, ")", 0, 0, 0);
	}
	else
	{
		elog(ERROR, "unrecognized node type: %d",
			 (int) nodeTag(setOp));
	}
}

/*
 * Display a sort/group clause.
 *
 * Also returns the expression tree, so caller need not find it again.
 */
static Node *
get_rule_sortgroupclause(Index ref, List *tlist, bool force_colno,
						 deparse_context *context)
{
	StringInfo	buf = context->buf;
	TargetEntry *tle;
	Node	   *expr;

	tle = get_sortgroupref_tle(ref, tlist);
	expr = (Node *) tle->expr;

	/*
	 * Use column-number form if requested by caller.  Otherwise, if
	 * expression is a constant, force it to be dumped with an explicit cast
	 * as decoration --- this is because a simple integer constant is
	 * ambiguous (and will be misinterpreted by findTargetlistEntry()) if we
	 * dump it without any decoration.  If it's anything more complex than a
	 * simple Var, then force extra parens around it, to ensure it can't be
	 * misinterpreted as a cube() or rollup() construct.
	 */
	if (force_colno)
	{
		Assert(!tle->resjunk);
		appendStringInfo(buf, "%d", tle->resno);
	}
	else if (expr && IsA(expr, Const))
		get_const_expr((Const *) expr, context, 1);
	else if (!expr || IsA(expr, Var))
		get_rule_expr(expr, context, true);
	else
	{
		/*
		 * We must force parens for function-like expressions even if
		 * PRETTY_PAREN is off, since those are the ones in danger of
		 * misparsing. For other expressions we need to force them only if
		 * PRETTY_PAREN is on, since otherwise the expression will output them
		 * itself. (We can't skip the parens.)
		 */
		bool		need_paren = (PRETTY_PAREN(context)
								  || IsA(expr, FuncExpr)
								  || IsA(expr, Aggref)
								  || IsA(expr, WindowFunc));

		if (need_paren)
			appendStringInfoChar(context->buf, '(');
		get_rule_expr(expr, context, true);
		if (need_paren)
			appendStringInfoChar(context->buf, ')');
	}

	return expr;
}

/*
 * Display a GroupingSet
 */
static void
get_rule_groupingset(GroupingSet *gset, List *targetlist,
					 bool omit_parens, deparse_context *context)
{
	ListCell   *l;
	StringInfo	buf = context->buf;
	bool		omit_child_parens = true;
	char	   *sep = "";

	switch (gset->kind)
	{
		case GROUPING_SET_EMPTY:
			appendStringInfoString(buf, "()");
			return;

		case GROUPING_SET_SIMPLE:
			{
				if (!omit_parens || list_length(gset->content) != 1)
					appendStringInfoChar(buf, '(');

				foreach(l, gset->content)
				{
					Index		ref = lfirst_int(l);

					appendStringInfoString(buf, sep);
					get_rule_sortgroupclause(ref, targetlist,
											 false, context);
					sep = ", ";
				}

				if (!omit_parens || list_length(gset->content) != 1)
					appendStringInfoChar(buf, ')');
			}
			return;

		case GROUPING_SET_ROLLUP:
			appendStringInfoString(buf, "ROLLUP(");
			break;
		case GROUPING_SET_CUBE:
			appendStringInfoString(buf, "CUBE(");
			break;
		case GROUPING_SET_SETS:
			appendStringInfoString(buf, "GROUPING SETS (");
			omit_child_parens = false;
			break;
	}

	foreach(l, gset->content)
	{
		appendStringInfoString(buf, sep);
		get_rule_groupingset(lfirst(l), targetlist, omit_child_parens, context);
		sep = ", ";
	}

	appendStringInfoChar(buf, ')');
}

/*
 * Display an ORDER BY list.
 */
static void
get_rule_orderby(List *orderList, List *targetList,
				 bool force_colno, deparse_context *context)
{
	StringInfo	buf = context->buf;
	const char *sep;
	ListCell   *l;

	sep = "";
	foreach(l, orderList)
	{
		SortGroupClause *srt = (SortGroupClause *) lfirst(l);
		Node	   *sortexpr;
		Oid			sortcoltype;
		TypeCacheEntry *typentry;

		appendStringInfoString(buf, sep);
		sortexpr = get_rule_sortgroupclause(srt->tleSortGroupRef, targetList,
											force_colno, context);
		sortcoltype = exprType(sortexpr);
		/* See whether operator is default < or > for datatype */
		typentry = lookup_type_cache(sortcoltype,
									 TYPECACHE_LT_OPR | TYPECACHE_GT_OPR);
		if (srt->sortop == typentry->lt_opr)
		{
			/* ASC is default, so emit nothing for it */
			if (srt->nulls_first)
				appendStringInfoString(buf, " NULLS FIRST");
		}
		else if (srt->sortop == typentry->gt_opr)
		{
			appendStringInfoString(buf, " DESC");
			/* DESC defaults to NULLS FIRST */
			if (!srt->nulls_first)
				appendStringInfoString(buf, " NULLS LAST");
		}
		else
		{
			appendStringInfo(buf, " USING %s",
							 generate_operator_name(srt->sortop,
													sortcoltype,
													sortcoltype));
			/* be specific to eliminate ambiguity */
			if (srt->nulls_first)
				appendStringInfoString(buf, " NULLS FIRST");
			else
				appendStringInfoString(buf, " NULLS LAST");
		}
		sep = ", ";
	}
}

/*
 * Display a WINDOW clause.
 *
 * Note that the windowClause list might contain only anonymous window
 * specifications, in which case we should print nothing here.
 */
static void
get_rule_windowclause(Query *query, deparse_context *context)
{
	StringInfo	buf = context->buf;
	const char *sep;
	ListCell   *l;

	sep = NULL;
	foreach(l, query->windowClause)
	{
		WindowClause *wc = (WindowClause *) lfirst(l);

		if (wc->name == NULL)
			continue;			/* ignore anonymous windows */

		if (sep == NULL)
			appendContextKeyword(context, " WINDOW ",
								 -PRETTYINDENT_STD, PRETTYINDENT_STD, 1);
		else
			appendStringInfoString(buf, sep);

		appendStringInfo(buf, "%s AS ", quote_identifier(wc->name));

		get_rule_windowspec(wc, query->targetList, context);

		sep = ", ";
	}
}

/*
 * Display a window definition
 */
static void
get_rule_windowspec(WindowClause *wc, List *targetList,
					deparse_context *context)
{
	StringInfo	buf = context->buf;
	bool		needspace = false;
	const char *sep;
	ListCell   *l;

	appendStringInfoChar(buf, '(');
	if (wc->refname)
	{
		appendStringInfoString(buf, quote_identifier(wc->refname));
		needspace = true;
	}
	/* partition clauses are always inherited, so only print if no refname */
	if (wc->partitionClause && !wc->refname)
	{
		if (needspace)
			appendStringInfoChar(buf, ' ');
		appendStringInfoString(buf, "PARTITION BY ");
		sep = "";
		foreach(l, wc->partitionClause)
		{
			SortGroupClause *grp = (SortGroupClause *) lfirst(l);

			appendStringInfoString(buf, sep);
			get_rule_sortgroupclause(grp->tleSortGroupRef, targetList,
									 false, context);
			sep = ", ";
		}
		needspace = true;
	}
	/* print ordering clause only if not inherited */
	if (wc->orderClause && !wc->copiedOrder)
	{
		if (needspace)
			appendStringInfoChar(buf, ' ');
		appendStringInfoString(buf, "ORDER BY ");
		get_rule_orderby(wc->orderClause, targetList, false, context);
		needspace = true;
	}
	/* framing clause is never inherited, so print unless it's default */
	if (wc->frameOptions & FRAMEOPTION_NONDEFAULT)
	{
		if (needspace)
			appendStringInfoChar(buf, ' ');
		if (wc->frameOptions & FRAMEOPTION_RANGE)
			appendStringInfoString(buf, "RANGE ");
		else if (wc->frameOptions & FRAMEOPTION_ROWS)
			appendStringInfoString(buf, "ROWS ");
		else if (wc->frameOptions & FRAMEOPTION_GROUPS)
			appendStringInfoString(buf, "GROUPS ");
		else
			Assert(false);
		if (wc->frameOptions & FRAMEOPTION_BETWEEN)
			appendStringInfoString(buf, "BETWEEN ");
		if (wc->frameOptions & FRAMEOPTION_START_UNBOUNDED_PRECEDING)
			appendStringInfoString(buf, "UNBOUNDED PRECEDING ");
		else if (wc->frameOptions & FRAMEOPTION_START_CURRENT_ROW)
			appendStringInfoString(buf, "CURRENT ROW ");
		else if (wc->frameOptions & FRAMEOPTION_START_OFFSET)
		{
			get_rule_expr(wc->startOffset, context, false);
			if (wc->frameOptions & FRAMEOPTION_START_OFFSET_PRECEDING)
				appendStringInfoString(buf, " PRECEDING ");
			else if (wc->frameOptions & FRAMEOPTION_START_OFFSET_FOLLOWING)
				appendStringInfoString(buf, " FOLLOWING ");
			else
				Assert(false);
		}
		else
			Assert(false);
		if (wc->frameOptions & FRAMEOPTION_BETWEEN)
		{
			appendStringInfoString(buf, "AND ");
			if (wc->frameOptions & FRAMEOPTION_END_UNBOUNDED_FOLLOWING)
				appendStringInfoString(buf, "UNBOUNDED FOLLOWING ");
			else if (wc->frameOptions & FRAMEOPTION_END_CURRENT_ROW)
				appendStringInfoString(buf, "CURRENT ROW ");
			else if (wc->frameOptions & FRAMEOPTION_END_OFFSET)
			{
				get_rule_expr(wc->endOffset, context, false);
				if (wc->frameOptions & FRAMEOPTION_END_OFFSET_PRECEDING)
					appendStringInfoString(buf, " PRECEDING ");
				else if (wc->frameOptions & FRAMEOPTION_END_OFFSET_FOLLOWING)
					appendStringInfoString(buf, " FOLLOWING ");
				else
					Assert(false);
			}
			else
				Assert(false);
		}
		if (wc->frameOptions & FRAMEOPTION_EXCLUDE_CURRENT_ROW)
			appendStringInfoString(buf, "EXCLUDE CURRENT ROW ");
		else if (wc->frameOptions & FRAMEOPTION_EXCLUDE_GROUP)
			appendStringInfoString(buf, "EXCLUDE GROUP ");
		else if (wc->frameOptions & FRAMEOPTION_EXCLUDE_TIES)
			appendStringInfoString(buf, "EXCLUDE TIES ");
		/* we will now have a trailing space; remove it */
		buf->len--;
	}
	appendStringInfoChar(buf, ')');
}

static Node *
find_param_referent(Param *param, deparse_context *context,
					deparse_namespace **dpns_p, ListCell **ancestor_cell_p)
{
	/* Initialize output parameters to prevent compiler warnings */
	*dpns_p = NULL;
	*ancestor_cell_p = NULL;

	/*
	 * If it's a PARAM_EXEC parameter, look for a matching NestLoopParam or
	 * SubPlan argument.  This will necessarily be in some ancestor of the
	 * current expression's Plan.
	 */
	if (param->paramkind == PARAM_EXEC)
	{
		deparse_namespace *dpns;
		Plan  *child_plan;
		bool		in_same_plan_level;
		ListCell   *lc;

		dpns = (deparse_namespace *) linitial(context->namespaces);
		child_plan = dpns->plan;
		in_same_plan_level = true;

		foreach(lc, dpns->ancestors)
		{
			Node	   *ancestor = (Node *) lfirst(lc);
			ListCell   *lc2;

			/*
			 * NestLoops transmit params to their inner child only; also, once
			 * we've crawled up out of a subplan, this couldn't possibly be
			 * the right match.
			 */
			if (IsA(ancestor, NestLoop) &&
				child_plan == innerPlan(ancestor) &&
				in_same_plan_level)
			{
				NestLoop   *nl = (NestLoop *) ancestor;

				foreach(lc2, nl->nestParams)
				{
					NestLoopParam *nlp = (NestLoopParam *) lfirst(lc2);

					if (nlp->paramno == param->paramid)
					{
						/* Found a match, so return it */
						*dpns_p = dpns;
						*ancestor_cell_p = lc;
						return (Node *) nlp->paramval;
					}
				}
			}

			/*
			 * Check to see if we're crawling up from a subplan.
			 */
			if(IsA(ancestor, SubPlan))
			{
				SubPlan    *subplan = (SubPlan *) ancestor;
				ListCell   *lc3;
				ListCell   *lc4;

				/* Matched subplan, so check its arguments */
				forboth(lc3, subplan->parParam, lc4, subplan->args)
				{
					int			paramid = lfirst_int(lc3);
					Node	   *arg = (Node *) lfirst(lc4);

					if (paramid == param->paramid)
					{
						/*
						 * Found a match, so return it.  But, since Vars in
						 * the arg are to be evaluated in the surrounding
						 * context, we have to point to the next ancestor item
						 * that is *not* a SubPlan.
						 */
						ListCell   *rest;

						for_each_cell(rest, dpns->ancestors,
									  lnext(dpns->ancestors, lc))
						{
							Node	   *ancestor2 = (Node *) lfirst(rest);

							if (!IsA(ancestor2, SubPlan))
							{
								*dpns_p = dpns;
								*ancestor_cell_p = rest;
								return arg;
							}
						}
						elog(ERROR, "SubPlan cannot be outermost ancestor");
					}
				}

				/* We have emerged from a subplan. */
				in_same_plan_level = false;

				/* SubPlan isn't a kind of Plan, so skip the rest */
				continue;
			}

			/*
			 * Check to see if we're emerging from an initplan of the current
			 * ancestor plan.  Initplans never have any parParams, so no need
			 * to search that list, but we need to know if we should reset
			 * in_same_plan_level.
			 */
			foreach(lc2, ((Plan *) ancestor)->initPlan)
			{
				SubPlan    *subplan = lfirst_node(SubPlan, lc2);

				if (child_plan != (Plan *) list_nth(dpns->subplans,
													subplan->plan_id - 1))
					continue;

				/* No parameters to be had here. */
				Assert(subplan->parParam == NIL);

				/* We have emerged from an initplan. */
				in_same_plan_level = false;
				break;
			}

			/* No luck, crawl up to next ancestor */
			child_plan = (Plan *) ancestor;
		}
	}

	/* No referent found */
	return NULL;
}


/*
 * appendContextKeyword - append a keyword to buffer
 *
 * If prettyPrint is enabled, perform a line break, and adjust indentation.
 * Otherwise, just append the keyword.
 */
static void
appendContextKeyword(deparse_context *context, const char *str,
					 int indentBefore, int indentAfter, int indentPlus)
{
	StringInfo	buf = context->buf;

	if (PRETTY_INDENT(context))
	{
		int			indentAmount;

		context->indentLevel += indentBefore;

		/* remove any trailing spaces currently in the buffer ... */
		removeStringInfoSpaces(buf);
		/* ... then add a newline and some spaces */
		appendStringInfoChar(buf, '\n');

		if (context->indentLevel < PRETTYINDENT_LIMIT)
			indentAmount = Max(context->indentLevel, 0) + indentPlus;
		else
		{
			/*
			 * If we're indented more than PRETTYINDENT_LIMIT characters, try
			 * to conserve horizontal space by reducing the per-level
			 * indentation.  For best results the scale factor here should
			 * divide all the indent amounts that get added to indentLevel
			 * (PRETTYINDENT_STD, etc).  It's important that the indentation
			 * not grow unboundedly, else deeply-nested trees use O(N^2)
			 * whitespace; so we also wrap modulo PRETTYINDENT_LIMIT.
			 */
			indentAmount = PRETTYINDENT_LIMIT +
				(context->indentLevel - PRETTYINDENT_LIMIT) /
				(PRETTYINDENT_STD / 2);
			indentAmount %= PRETTYINDENT_LIMIT;
			/* scale/wrap logic affects indentLevel, but not indentPlus */
			indentAmount += indentPlus;
		}
		appendStringInfoSpaces(buf, indentAmount);

		appendStringInfoString(buf, str);

		context->indentLevel += indentAfter;
		if (context->indentLevel < 0)
			context->indentLevel = 0;
	}
	else
		appendStringInfoString(buf, str);
}

/*
 * removeStringInfoSpaces - delete trailing spaces from a buffer.
 *
 * Possibly this should move to stringinfo.c at some point.
 */
static void
removeStringInfoSpaces(StringInfo str)
{
	while (str->len > 0 && str->data[str->len - 1] == ' ')
		str->data[--(str->len)] = '\0';
}

/*
 * get_rule_expr_paren	- deparse expr using get_rule_expr,
 * embracing the string with parentheses if necessary for prettyPrint.
 *
 * Never embrace if prettyFlags=0, because it's done in the calling node.
 *
 * Any node that does *not* embrace its argument node by sql syntax (with
 * parentheses, non-operator keywords like CASE/WHEN/ON, or comma etc) should
 * use get_rule_expr_paren instead of get_rule_expr so parentheses can be
 * added.
 */
static void
get_rule_expr_paren(Node *node, deparse_context *context,
					bool showimplicit, Node *parentNode)
{
	bool		need_paren;

	need_paren = PRETTY_PAREN(context) &&
		!isSimpleNode(node, parentNode, context->prettyFlags);

	if (need_paren)
		appendStringInfoChar(context->buf, '(');

	get_rule_expr(node, context, showimplicit);

	if (need_paren)
		appendStringInfoChar(context->buf, ')');
}


/*
 * appendContextKeyword - append a keyword to buffer
 *
 * If prettyPrint is enabled, perform a line break, and adjust indentation.
 * Otherwise, just append the keyword.
 */
static void
appendContextKeyword(deparse_context *context, const char *str,
					 int indentBefore, int indentAfter, int indentPlus)
{
	StringInfo	buf = context->buf;

	if (PRETTY_INDENT(context))
	{
		int			indentAmount;

		context->indentLevel += indentBefore;

		/* remove any trailing spaces currently in the buffer ... */
		removeStringInfoSpaces(buf);
		/* ... then add a newline and some spaces */
		appendStringInfoChar(buf, '\n');

		if (context->indentLevel < PRETTYINDENT_LIMIT)
			indentAmount = Max(context->indentLevel, 0) + indentPlus;
		else
		{
			/*
			 * If we're indented more than PRETTYINDENT_LIMIT characters, try
			 * to conserve horizontal space by reducing the per-level
			 * indentation.  For best results the scale factor here should
			 * divide all the indent amounts that get added to indentLevel
			 * (PRETTYINDENT_STD, etc).  It's important that the indentation
			 * not grow unboundedly, else deeply-nested trees use O(N^2)
			 * whitespace; so we also wrap modulo PRETTYINDENT_LIMIT.
			 */
			indentAmount = PRETTYINDENT_LIMIT +
				(context->indentLevel - PRETTYINDENT_LIMIT) /
				(PRETTYINDENT_STD / 2);
			indentAmount %= PRETTYINDENT_LIMIT;
			/* scale/wrap logic affects indentLevel, but not indentPlus */
			indentAmount += indentPlus;
		}
		appendStringInfoSpaces(buf, indentAmount);

		appendStringInfoString(buf, str);

		context->indentLevel += indentAfter;
		if (context->indentLevel < 0)
			context->indentLevel = 0;
	}
	else
		appendStringInfoString(buf, str);
}

/*
 * removeStringInfoSpaces - delete trailing spaces from a buffer.
 *
 * Possibly this should move to stringinfo.c at some point.
 */
static void
removeStringInfoSpaces(StringInfo str)
{
	while (str->len > 0 && str->data[str->len - 1] == ' ')
		str->data[--(str->len)] = '\0';
}


/*
 * get_rule_expr_paren	- deparse expr using get_rule_expr,
 * embracing the string with parentheses if necessary for prettyPrint.
 *
 * Never embrace if prettyFlags=0, because it's done in the calling node.
 *
 * Any node that does *not* embrace its argument node by sql syntax (with
 * parentheses, non-operator keywords like CASE/WHEN/ON, or comma etc) should
 * use get_rule_expr_paren instead of get_rule_expr so parentheses can be
 * added.
 */
static void
get_rule_expr_paren(Node *node, deparse_context *context,
					bool showimplicit, Node *parentNode)
{
	bool		need_paren;

	need_paren = PRETTY_PAREN(context) &&
		!isSimpleNode(node, parentNode, context->prettyFlags);

	if (need_paren)
		appendStringInfoChar(context->buf, '(');

	get_rule_expr(node, context, showimplicit);

	if (need_paren)
		appendStringInfoChar(context->buf, ')');
}

/*
 * Try to find the referenced expression for a PARAM_EXEC Param that might
 * reference a parameter supplied by an upper NestLoop or SubPlan plan node.
 *
 * If successful, return the expression and set *dpns_p and *ancestor_cell_p
 * appropriately for calling push_ancestor_plan().  If no referent can be
 * found, return NULL.
 */
static Node *
find_param_referent(Param *param, deparse_context *context,
					deparse_namespace **dpns_p, ListCell **ancestor_cell_p)
{
	/* Initialize output parameters to prevent compiler warnings */
	*dpns_p = NULL;
	*ancestor_cell_p = NULL;

	/*
	 * If it's a PARAM_EXEC parameter, look for a matching NestLoopParam or
	 * SubPlan argument.  This will necessarily be in some ancestor of the
	 * current expression's Plan.
	 */
	if (param->paramkind == PARAM_EXEC)
	{
		deparse_namespace *dpns;
		Plan  *child_plan;
		bool		in_same_plan_level;
		ListCell   *lc;

		dpns = (deparse_namespace *) linitial(context->namespaces);
		child_plan = dpns->plan;
		in_same_plan_level = true;

		foreach(lc, dpns->ancestors)
		{
			Node	   *ancestor = (Node *) lfirst(lc);
			ListCell   *lc2;

			/*
			 * NestLoops transmit params to their inner child only; also, once
			 * we've crawled up out of a subplan, this couldn't possibly be
			 * the right match.
			 */
			if (IsA(ancestor, NestLoop) &&
				child_plan == innerPlan(ancestor) &&
				in_same_plan_level)
			{
				NestLoop   *nl = (NestLoop *) ancestor;

				foreach(lc2, nl->nestParams)
				{
					NestLoopParam *nlp = (NestLoopParam *) lfirst(lc2);

					if (nlp->paramno == param->paramid)
					{
						/* Found a match, so return it */
						*dpns_p = dpns;
						*ancestor_cell_p = lc;
						return (Node *) nlp->paramval;
					}
				}
			}

			/*
			 * Check to see if we're crawling up from a subplan.
			 */
			if(IsA(ancestor, SubPlan))
			{
				SubPlan    *subplan = (SubPlan *) ancestor;
				ListCell   *lc3;
				ListCell   *lc4;

				/* Matched subplan, so check its arguments */
				forboth(lc3, subplan->parParam, lc4, subplan->args)
				{
					int			paramid = lfirst_int(lc3);
					Node	   *arg = (Node *) lfirst(lc4);

					if (paramid == param->paramid)
					{
						/*
						 * Found a match, so return it.  But, since Vars in
						 * the arg are to be evaluated in the surrounding
						 * context, we have to point to the next ancestor item
						 * that is *not* a SubPlan.
						 */
						ListCell   *rest;

						for_each_cell(rest, dpns->ancestors,
									  lnext(dpns->ancestors, lc))
						{
							Node	   *ancestor2 = (Node *) lfirst(rest);

							if (!IsA(ancestor2, SubPlan))
							{
								*dpns_p = dpns;
								*ancestor_cell_p = rest;
								return arg;
							}
						}
						elog(ERROR, "SubPlan cannot be outermost ancestor");
					}
				}

				/* We have emerged from a subplan. */
				in_same_plan_level = false;

				/* SubPlan isn't a kind of Plan, so skip the rest */
				continue;
			}

			/*
			 * Check to see if we're emerging from an initplan of the current
			 * ancestor plan.  Initplans never have any parParams, so no need
			 * to search that list, but we need to know if we should reset
			 * in_same_plan_level.
			 */
			foreach(lc2, ((Plan *) ancestor)->initPlan)
			{
				SubPlan    *subplan = lfirst_node(SubPlan, lc2);

				if (child_plan != (Plan *) list_nth(dpns->subplans,
													subplan->plan_id - 1))
					continue;

				/* No parameters to be had here. */
				Assert(subplan->parParam == NIL);

				/* We have emerged from an initplan. */
				in_same_plan_level = false;
				break;
			}

			/* No luck, crawl up to next ancestor */
			child_plan = (Plan *) ancestor;
		}
	}

	/* No referent found */
	return NULL;
}


/*
 * Display a sort/group clause.
 *
 * Also returns the expression tree, so caller need not find it again.
 */
static Node *
get_rule_sortgroupclause(Index ref, List *tlist, bool force_colno,
						 deparse_context *context)
{
	StringInfo	buf = context->buf;
	TargetEntry *tle;
	Node	   *expr;

	tle = get_sortgroupref_tle(ref, tlist);
	expr = (Node *) tle->expr;

	/*
	 * Use column-number form if requested by caller.  Otherwise, if
	 * expression is a constant, force it to be dumped with an explicit cast
	 * as decoration --- this is because a simple integer constant is
	 * ambiguous (and will be misinterpreted by findTargetlistEntry()) if we
	 * dump it without any decoration.  If it's anything more complex than a
	 * simple Var, then force extra parens around it, to ensure it can't be
	 * misinterpreted as a cube() or rollup() construct.
	 */
	if (force_colno)
	{
		Assert(!tle->resjunk);
		appendStringInfo(buf, "%d", tle->resno);
	}
	else if (expr && IsA(expr, Const))
		get_const_expr((Const *) expr, context, 1);
	else if (!expr || IsA(expr, Var))
		get_rule_expr(expr, context, true);
	else
	{
		/*
		 * We must force parens for function-like expressions even if
		 * PRETTY_PAREN is off, since those are the ones in danger of
		 * misparsing. For other expressions we need to force them only if
		 * PRETTY_PAREN is on, since otherwise the expression will output them
		 * itself. (We can't skip the parens.)
		 */
		bool		need_paren = (PRETTY_PAREN(context)
								  || IsA(expr, FuncExpr)
								  ||IsA(expr, Aggref)
								  ||IsA(expr, WindowFunc));

		if (need_paren)
			appendStringInfoChar(context->buf, '(');
		get_rule_expr(expr, context, true);
		if (need_paren)
			appendStringInfoChar(context->buf, ')');
	}

	return expr;
}

/*
 * Display a GroupingSet
 */
static void
get_rule_groupingset(GroupingSet *gset, List *targetlist,
					 bool omit_parens, deparse_context *context)
{
	ListCell   *l;
	StringInfo	buf = context->buf;
	bool		omit_child_parens = true;
	char	   *sep = "";

	switch (gset->kind)
	{
		case GROUPING_SET_EMPTY:
			appendStringInfoString(buf, "()");
			return;

		case GROUPING_SET_SIMPLE:
			{
				if (!omit_parens || list_length(gset->content) != 1)
					appendStringInfoChar(buf, '(');

				foreach(l, gset->content)
				{
					Index		ref = lfirst_int(l);

					appendStringInfoString(buf, sep);
					get_rule_sortgroupclause(ref, targetlist,
											 false, context);
					sep = ", ";
				}

				if (!omit_parens || list_length(gset->content) != 1)
					appendStringInfoChar(buf, ')');
			}
			return;

		case GROUPING_SET_ROLLUP:
			appendStringInfoString(buf, "ROLLUP(");
			break;
		case GROUPING_SET_CUBE:
			appendStringInfoString(buf, "CUBE(");
			break;
		case GROUPING_SET_SETS:
			appendStringInfoString(buf, "GROUPING SETS (");
			omit_child_parens = false;
			break;
	}

	foreach(l, gset->content)
	{
		appendStringInfoString(buf, sep);
		get_rule_groupingset(lfirst(l), targetlist, omit_child_parens, context);
		sep = ", ";
	}

	appendStringInfoChar(buf, ')');
}

/*
 * Display an ORDER BY list.
 */
static void
get_rule_orderby(List *orderList, List *targetList,
				 bool force_colno, deparse_context *context)
{
	StringInfo	buf = context->buf;
	const char *sep;
	ListCell   *l;

	sep = "";
	foreach(l, orderList)
	{
		SortGroupClause *srt = (SortGroupClause *) lfirst(l);
		Node	   *sortexpr;
		Oid			sortcoltype;
		TypeCacheEntry *typentry;

		appendStringInfoString(buf, sep);
		sortexpr = get_rule_sortgroupclause(srt->tleSortGroupRef, targetList,
											force_colno, context);
		sortcoltype = exprType(sortexpr);
		/* See whether operator is default < or > for datatype */
		typentry = lookup_type_cache(sortcoltype,
									 TYPECACHE_LT_OPR | TYPECACHE_GT_OPR);
		if (srt->sortop == typentry->lt_opr)
		{
			/* ASC is default, so emit nothing for it */
			if (srt->nulls_first)
				appendStringInfoString(buf, " NULLS FIRST");
		}
		else if (srt->sortop == typentry->gt_opr)
		{
			appendStringInfoString(buf, " DESC");
			/* DESC defaults to NULLS FIRST */
			if (!srt->nulls_first)
				appendStringInfoString(buf, " NULLS LAST");
		}
		else
		{
			appendStringInfo(buf, " USING %s",
							 generate_operator_name(srt->sortop,
													sortcoltype,
													sortcoltype));
			/* be specific to eliminate ambiguity */
			if (srt->nulls_first)
				appendStringInfoString(buf, " NULLS FIRST");
			else
				appendStringInfoString(buf, " NULLS LAST");
		}
		sep = ", ";
	}
}

/*
 * Display a WINDOW clause.
 *
 * Note that the windowClause list might contain only anonymous window
 * specifications, in which case we should print nothing here.
 */
static void
get_rule_windowclause(Query *query, deparse_context *context)
{
	StringInfo	buf = context->buf;
	const char *sep;
	ListCell   *l;

	sep = NULL;
	foreach(l, query->windowClause)
	{
		WindowClause *wc = (WindowClause *) lfirst(l);

		if (wc->name == NULL)
			continue;			/* ignore anonymous windows */

		if (sep == NULL)
			appendContextKeyword(context, " WINDOW ",
								 -PRETTYINDENT_STD, PRETTYINDENT_STD, 1);
		else
			appendStringInfoString(buf, sep);

		appendStringInfo(buf, "%s AS ", quote_identifier(wc->name));

		get_rule_windowspec(wc, query->targetList, context);

		sep = ", ";
	}
}

/*
 * Display a window definition
 */
static void
get_rule_windowspec(WindowClause *wc, List *targetList,
					deparse_context *context)
{
	StringInfo	buf = context->buf;
	bool		needspace = false;
	const char *sep;
	ListCell   *l;

	appendStringInfoChar(buf, '(');
	if (wc->refname)
	{
		appendStringInfoString(buf, quote_identifier(wc->refname));
		needspace = true;
	}
	/* partition clauses are always inherited, so only print if no refname */
	if (wc->partitionClause && !wc->refname)
	{
		if (needspace)
			appendStringInfoChar(buf, ' ');
		appendStringInfoString(buf, "PARTITION BY ");
		sep = "";
		foreach(l, wc->partitionClause)
		{
			SortGroupClause *grp = (SortGroupClause *) lfirst(l);

			appendStringInfoString(buf, sep);
			get_rule_sortgroupclause(grp->tleSortGroupRef, targetList,
									 false, context);
			sep = ", ";
		}
		needspace = true;
	}
	/* print ordering clause only if not inherited */
	if (wc->orderClause && !wc->copiedOrder)
	{
		if (needspace)
			appendStringInfoChar(buf, ' ');
		appendStringInfoString(buf, "ORDER BY ");
		get_rule_orderby(wc->orderClause, targetList, false, context);
		needspace = true;
	}
	/* framing clause is never inherited, so print unless it's default */
	if (wc->frameOptions & FRAMEOPTION_NONDEFAULT)
	{
		if (needspace)
			appendStringInfoChar(buf, ' ');
		if (wc->frameOptions & FRAMEOPTION_RANGE)
			appendStringInfoString(buf, "RANGE ");
		else if (wc->frameOptions & FRAMEOPTION_ROWS)
			appendStringInfoString(buf, "ROWS ");
		else if (wc->frameOptions & FRAMEOPTION_GROUPS)
			appendStringInfoString(buf, "GROUPS ");
		else
			Assert(false);
		if (wc->frameOptions & FRAMEOPTION_BETWEEN)
			appendStringInfoString(buf, "BETWEEN ");
		if (wc->frameOptions & FRAMEOPTION_START_UNBOUNDED_PRECEDING)
			appendStringInfoString(buf, "UNBOUNDED PRECEDING ");
		else if (wc->frameOptions & FRAMEOPTION_START_CURRENT_ROW)
			appendStringInfoString(buf, "CURRENT ROW ");
		else if (wc->frameOptions & FRAMEOPTION_START_OFFSET)
		{
			get_rule_expr(wc->startOffset, context, false);
			if (wc->frameOptions & FRAMEOPTION_START_OFFSET_PRECEDING)
				appendStringInfoString(buf, " PRECEDING ");
			else if (wc->frameOptions & FRAMEOPTION_START_OFFSET_FOLLOWING)
				appendStringInfoString(buf, " FOLLOWING ");
			else
				Assert(false);
		}
		else
			Assert(false);
		if (wc->frameOptions & FRAMEOPTION_BETWEEN)
		{
			appendStringInfoString(buf, "AND ");
			if (wc->frameOptions & FRAMEOPTION_END_UNBOUNDED_FOLLOWING)
				appendStringInfoString(buf, "UNBOUNDED FOLLOWING ");
			else if (wc->frameOptions & FRAMEOPTION_END_CURRENT_ROW)
				appendStringInfoString(buf, "CURRENT ROW ");
			else if (wc->frameOptions & FRAMEOPTION_END_OFFSET)
			{
				get_rule_expr(wc->endOffset, context, false);
				if (wc->frameOptions & FRAMEOPTION_END_OFFSET_PRECEDING)
					appendStringInfoString(buf, " PRECEDING ");
				else if (wc->frameOptions & FRAMEOPTION_END_OFFSET_FOLLOWING)
					appendStringInfoString(buf, " FOLLOWING ");
				else
					Assert(false);
			}
			else
				Assert(false);
		}
		if (wc->frameOptions & FRAMEOPTION_EXCLUDE_CURRENT_ROW)
			appendStringInfoString(buf, "EXCLUDE CURRENT ROW ");
		else if (wc->frameOptions & FRAMEOPTION_EXCLUDE_GROUP)
			appendStringInfoString(buf, "EXCLUDE GROUP ");
		else if (wc->frameOptions & FRAMEOPTION_EXCLUDE_TIES)
			appendStringInfoString(buf, "EXCLUDE TIES ");
		/* we will now have a trailing space; remove it */
		buf->len--;
	}
	appendStringInfoChar(buf, ')');
}

/* ----------
 * get_coercion_expr
 *
 *	Make a string representation of a value coerced to a specific type
 * ----------
 */
static void
get_coercion_expr(Node *arg, deparse_context *context,
				  Oid resulttype, int32 resulttypmod,
				  Node *parentNode)
{
	StringInfo	buf = context->buf;

	/*
	 * Since parse_coerce.c doesn't immediately collapse application of
	 * length-coercion functions to constants, what we'll typically see in
	 * such cases is a Const with typmod -1 and a length-coercion function
	 * right above it.  Avoid generating redundant output. However, beware of
	 * suppressing casts when the user actually wrote something like
	 * 'foo'::text::char(3).
	 *
	 * Note: it might seem that we are missing the possibility of needing to
	 * print a COLLATE clause for such a Const.  However, a Const could only
	 * have nondefault collation in a post-constant-folding tree, in which the
	 * length coercion would have been folded too.  See also the special
	 * handling of CollateExpr in coerce_to_target_type(): any collation
	 * marking will be above the coercion node, not below it.
	 */
	if (arg && IsA(arg, Const) &&
		((Const *) arg)->consttype == resulttype &&
		((Const *) arg)->consttypmod == -1)
	{
		/* Show the constant without normal ::typename decoration */
		get_const_expr((Const *) arg, context, -1);
	}
	else
	{
		if (!PRETTY_PAREN(context))
			appendStringInfoChar(buf, '(');
		get_rule_expr_paren(arg, context, false, parentNode);
		if (!PRETTY_PAREN(context))
			appendStringInfoChar(buf, ')');
	}
	appendStringInfo(buf, "::%s",
					 format_type_with_typemod(resulttype, resulttypmod));
}


/*
 * appendContextKeyword - append a keyword to buffer
 *
 * If prettyPrint is enabled, perform a line break, and adjust indentation.
 * Otherwise, just append the keyword.
 */
static void
appendContextKeyword(deparse_context *context, const char *str,
					 int indentBefore, int indentAfter, int indentPlus)
{
	StringInfo	buf = context->buf;

	if (PRETTY_INDENT(context))
	{
		int			indentAmount;

		context->indentLevel += indentBefore;

		/* remove any trailing spaces currently in the buffer ... */
		removeStringInfoSpaces(buf);
		/* ... then add a newline and some spaces */
		appendStringInfoChar(buf, '\n');

		if (context->indentLevel < PRETTYINDENT_LIMIT)
			indentAmount = Max(context->indentLevel, 0) + indentPlus;
		else
		{
			/*
			 * If we're indented more than PRETTYINDENT_LIMIT characters, try
			 * to conserve horizontal space by reducing the per-level
			 * indentation.  For best results the scale factor here should
			 * divide all the indent amounts that get added to indentLevel
			 * (PRETTYINDENT_STD, etc).  It's important that the indentation
			 * not grow unboundedly, else deeply-nested trees use O(N^2)
			 * whitespace; so we also wrap modulo PRETTYINDENT_LIMIT.
			 */
			indentAmount = PRETTYINDENT_LIMIT +
				(context->indentLevel - PRETTYINDENT_LIMIT) /
				(PRETTYINDENT_STD / 2);
			indentAmount %= PRETTYINDENT_LIMIT;
			/* scale/wrap logic affects indentLevel, but not indentPlus */
			indentAmount += indentPlus;
		}
		appendStringInfoSpaces(buf, indentAmount);

		appendStringInfoString(buf, str);

		context->indentLevel += indentAfter;
		if (context->indentLevel < 0)
			context->indentLevel = 0;
	}
	else
		appendStringInfoString(buf, str);
}

/*
 * removeStringInfoSpaces - delete trailing spaces from a buffer.
 *
 * Possibly this should move to stringinfo.c at some point.
 */
static void
removeStringInfoSpaces(StringInfo str)
{
	while (str->len > 0 && str->data[str->len - 1] == ' ')
		str->data[--(str->len)] = '\0';
}


/*
 * get_rule_expr_paren	- deparse expr using get_rule_expr,
 * embracing the string with parentheses if necessary for prettyPrint.
 *
 * Never embrace if prettyFlags=0, because it's done in the calling node.
 *
 * Any node that does *not* embrace its argument node by sql syntax (with
 * parentheses, non-operator keywords like CASE/WHEN/ON, or comma etc) should
 * use get_rule_expr_paren instead of get_rule_expr so parentheses can be
 * added.
 */
static void
get_rule_expr_paren(Node *node, deparse_context *context,
					bool showimplicit, Node *parentNode)
{
	bool		need_paren;

	need_paren = PRETTY_PAREN(context) &&
		!isSimpleNode(node, parentNode, context->prettyFlags);

	if (need_paren)
		appendStringInfoChar(context->buf, '(');

	get_rule_expr(node, context, showimplicit);

	if (need_paren)
		appendStringInfoChar(context->buf, ')');
}

static void
get_basic_select_query(Query *query, deparse_context *context,
					   TupleDesc resultDesc, bool colNamesVisible)
{
	StringInfo	buf = context->buf;
	RangeTblEntry *values_rte;
	char	   *sep;
	ListCell   *l;

	if (PRETTY_INDENT(context))
	{
		context->indentLevel += PRETTYINDENT_STD;
		appendStringInfoChar(buf, ' ');
	}

	/*
	 * If the query looks like SELECT * FROM (VALUES ...), then print just the
	 * VALUES part.  This reverses what transformValuesClause() did at parse
	 * time.
	 */
	values_rte = get_simple_values_rte(query, resultDesc);
	if (values_rte)
	{
		get_values_def(values_rte->values_lists, context);
		return;
	}

	/*
	 * Build up the query string - first we say SELECT
	 */
	if (query->isReturn)
		appendStringInfoString(buf, "RETURN");
	else
		appendStringInfoString(buf, "SELECT");

	/* Add the DISTINCT clause if given */
	if (query->distinctClause != NIL)
	{
		if (query->hasDistinctOn)
		{
			appendStringInfoString(buf, " DISTINCT ON (");
			sep = "";
			foreach(l, query->distinctClause)
			{
				SortGroupClause *srt = (SortGroupClause *) lfirst(l);

				appendStringInfoString(buf, sep);
				get_rule_sortgroupclause(srt->tleSortGroupRef, query->targetList,
										 false, context);
				sep = ", ";
			}
			appendStringInfoChar(buf, ')');
		}
		else
			appendStringInfoString(buf, " DISTINCT");
	}

	/* Then we tell what to select (the targetlist) */
	get_target_list(query->targetList, context, resultDesc, colNamesVisible);

	/* Add the FROM clause if needed */
	get_from_clause(query, " FROM ", context);

	/* Add the WHERE clause if given */
	if (query->jointree->quals != NULL)
	{
		appendContextKeyword(context, " WHERE ",
							 -PRETTYINDENT_STD, PRETTYINDENT_STD, 1);
		get_rule_expr(query->jointree->quals, context, false);
	}

	/* Add the GROUP BY clause if given */
	if (query->groupClause != NULL || query->groupingSets != NULL)
	{
		ParseExprKind save_exprkind;

		appendContextKeyword(context, " GROUP BY ",
							 -PRETTYINDENT_STD, PRETTYINDENT_STD, 1);
		if (query->groupDistinct)
			appendStringInfoString(buf, "DISTINCT ");

		save_exprkind = context->special_exprkind;
		context->special_exprkind = EXPR_KIND_GROUP_BY;

		if (query->groupingSets == NIL)
		{
			sep = "";
			foreach(l, query->groupClause)
			{
				SortGroupClause *grp = (SortGroupClause *) lfirst(l);

				appendStringInfoString(buf, sep);
				get_rule_sortgroupclause(grp->tleSortGroupRef, query->targetList,
										 false, context);
				sep = ", ";
			}
		}
		else
		{
			sep = "";
			foreach(l, query->groupingSets)
			{
				GroupingSet *grp = lfirst(l);

				appendStringInfoString(buf, sep);
				get_rule_groupingset(grp, query->targetList, true, context);
				sep = ", ";
			}
		}

		context->special_exprkind = save_exprkind;
	}

	/* Add the HAVING clause if given */
	if (query->havingQual != NULL)
	{
		appendContextKeyword(context, " HAVING ",
							 -PRETTYINDENT_STD, PRETTYINDENT_STD, 0);
		get_rule_expr(query->havingQual, context, false);
	}

	/* Add the WINDOW clause if needed */
	if (query->windowClause != NIL)
		get_rule_windowclause(query, context);
}


/* ----------
 * get_values_def			- Parse back a VALUES list
 * ----------
 */
static void
get_values_def(List *values_lists, deparse_context *context)
{
	StringInfo	buf = context->buf;
	bool		first_list = true;
	ListCell   *vtl;

	appendStringInfoString(buf, "VALUES ");

	foreach(vtl, values_lists)
	{
		List	   *sublist = (List *) lfirst(vtl);
		bool		first_col = true;
		ListCell   *lc;

		if (first_list)
			first_list = false;
		else
			appendStringInfoString(buf, ", ");

		appendStringInfoChar(buf, '(');
		foreach(lc, sublist)
		{
			Node	   *col = (Node *) lfirst(lc);

			if (first_col)
				first_col = false;
			else
				appendStringInfoChar(buf, ',');

			/*
			 * Print the value.  Whole-row Vars need special treatment.
			 */
			get_rule_expr_toplevel(col, context, false);
		}
		appendStringInfoChar(buf, ')');
	}
}

/* ----------
 * get_with_clause			- Parse back a WITH clause
 * ----------
 */
static void
get_with_clause(Query *query, deparse_context *context)
{
	StringInfo	buf = context->buf;
	const char *sep;
	ListCell   *l;

	if (query->cteList == NIL)
		return;

	if (PRETTY_INDENT(context))
	{
		context->indentLevel += PRETTYINDENT_STD;
		appendStringInfoChar(buf, ' ');
	}

	if (query->hasRecursive)
		sep = "WITH RECURSIVE ";
	else
		sep = "WITH ";
	foreach(l, query->cteList)
	{
		CommonTableExpr *cte = (CommonTableExpr *) lfirst(l);

		appendStringInfoString(buf, sep);
		appendStringInfoString(buf, quote_identifier(cte->ctename));
		if (cte->aliascolnames)
		{
			bool		first = true;
			ListCell   *col;

			appendStringInfoChar(buf, '(');
			foreach(col, cte->aliascolnames)
			{
				if (first)
					first = false;
				else
					appendStringInfoString(buf, ", ");
				appendStringInfoString(buf,
									   quote_identifier(strVal(lfirst(col))));
			}
			appendStringInfoChar(buf, ')');
		}
		appendStringInfoString(buf, " AS ");
		switch (cte->ctematerialized)
		{
			case CTEMaterializeDefault:
				break;
			case CTEMaterializeAlways:
				appendStringInfoString(buf, "MATERIALIZED ");
				break;
			case CTEMaterializeNever:
				appendStringInfoString(buf, "NOT MATERIALIZED ");
				break;
		}
		appendStringInfoChar(buf, '(');
		if (PRETTY_INDENT(context))
			appendContextKeyword(context, "", 0, 0, 0);
		get_query_def((Query *) cte->ctequery, buf, context->namespaces, NULL,
		              true,
					  context->prettyFlags, context->wrapColumn,
					  context->indentLevel);
		if (PRETTY_INDENT(context))
			appendContextKeyword(context, "", 0, 0, 0);
		appendStringInfoChar(buf, ')');

		if (cte->search_clause)
		{
			bool		first = true;
			ListCell   *lc;

			appendStringInfo(buf, " SEARCH %s FIRST BY ",
							 cte->search_clause->search_breadth_first ? "BREADTH" : "DEPTH");

			foreach(lc, cte->search_clause->search_col_list)
			{
				if (first)
					first = false;
				else
					appendStringInfoString(buf, ", ");
				appendStringInfoString(buf,
									   quote_identifier(strVal(lfirst(lc))));
			}

			appendStringInfo(buf, " SET %s", quote_identifier(cte->search_clause->search_seq_column));
		}

		if (cte->cycle_clause)
		{
			bool		first = true;
			ListCell   *lc;

			appendStringInfoString(buf, " CYCLE ");

			foreach(lc, cte->cycle_clause->cycle_col_list)
			{
				if (first)
					first = false;
				else
					appendStringInfoString(buf, ", ");
				appendStringInfoString(buf,
									   quote_identifier(strVal(lfirst(lc))));
			}

			appendStringInfo(buf, " SET %s", quote_identifier(cte->cycle_clause->cycle_mark_column));

			{
				Const	   *cmv = castNode(Const, cte->cycle_clause->cycle_mark_value);
				Const	   *cmd = castNode(Const, cte->cycle_clause->cycle_mark_default);

				if (!(cmv->consttype == BOOLOID && !cmv->constisnull && DatumGetBool(cmv->constvalue) == true &&
					  cmd->consttype == BOOLOID && !cmd->constisnull && DatumGetBool(cmd->constvalue) == false))
				{
					appendStringInfoString(buf, " TO ");
					get_rule_expr(cte->cycle_clause->cycle_mark_value, context, false);
					appendStringInfoString(buf, " DEFAULT ");
					get_rule_expr(cte->cycle_clause->cycle_mark_default, context, false);
				}
			}

			appendStringInfo(buf, " USING %s", quote_identifier(cte->cycle_clause->cycle_path_column));
		}

		sep = ", ";
	}

	if (PRETTY_INDENT(context))
	{
		context->indentLevel -= PRETTYINDENT_STD;
		appendContextKeyword(context, "", 0, 0, 0);
	}
	else
		appendStringInfoChar(buf, ' ');
}

/*
 * get_agg_expr			- Parse back an Aggref node
 */
static void
get_agg_expr(Aggref *aggref, deparse_context *context,
			 Aggref *original_aggref)
{
	StringInfo	buf = context->buf;
	Oid			argtypes[FUNC_MAX_ARGS];
	int			nargs;
	bool		use_variadic;

	/*
	 * For a combining aggregate, we look up and deparse the corresponding
	 * partial aggregate instead.  This is necessary because our input
	 * argument list has been replaced; the new argument list always has just
	 * one element, which will point to a partial Aggref that supplies us with
	 * transition states to combine.
	 */
	if (DO_AGGSPLIT_COMBINE(aggref->aggsplit))
	{
		TargetEntry *tle;


		Assert(list_length(aggref->args) == 1);
		tle = linitial_node(TargetEntry, aggref->args);
		resolve_special_varno((Node *) tle->expr, context,
							  get_agg_combine_expr, original_aggref);
		return;
	}

	/*
	 * Mark as PARTIAL, if appropriate.  We look to the original aggref so as
	 * to avoid printing this when recursing from the code just above.
	 */
	if (DO_AGGSPLIT_SKIPFINAL(original_aggref->aggsplit))
		appendStringInfoString(buf, "PARTIAL ");

	/* Extract the argument types as seen by the parser */
	nargs = get_aggregate_argtypes(aggref, argtypes);

	/* Print the aggregate name, schema-qualified if needed */
	appendStringInfo(buf, "%s(%s",
					 generate_function_name(aggref->aggfnoid, nargs,
											NIL, argtypes,
											aggref->aggvariadic,
											&use_variadic,
											context->special_exprkind),
					 (aggref->aggdistinct != NIL) ? "DISTINCT " : "");

	if (AGGKIND_IS_ORDERED_SET(aggref->aggkind))
	{
		/*
		 * Ordered-set aggregates do not use "*" syntax.  Also, we needn't
		 * worry about inserting VARIADIC.  So we can just dump the direct
		 * args as-is.
		 */
		Assert(!aggref->aggvariadic);
		get_rule_expr((Node *) aggref->aggdirectargs, context, true);
		Assert(aggref->aggorder != NIL);
		appendStringInfoString(buf, ") WITHIN GROUP (ORDER BY ");
		get_rule_orderby(aggref->aggorder, aggref->args, false, context);
	}
	else
	{
		/* aggstar can be set only in zero-argument aggregates */
		if (aggref->aggstar)
			appendStringInfoChar(buf, '*');
		else
		{
			ListCell   *l;
			int			i;

			i = 0;
			foreach(l, aggref->args)
			{
				TargetEntry *tle = (TargetEntry *) lfirst(l);
				Node	   *arg = (Node *) tle->expr;

				Assert(!IsA(arg, NamedArgExpr));
				if (tle->resjunk)
					continue;
				if (i++ > 0)
					appendStringInfoString(buf, ", ");
				if (use_variadic && i == nargs)
					appendStringInfoString(buf, "VARIADIC ");
				get_rule_expr(arg, context, true);
			}
		}

		if (aggref->aggorder != NIL)
		{
			appendStringInfoString(buf, " ORDER BY ");
			get_rule_orderby(aggref->aggorder, aggref->args, false, context);
		}
	}

	if (aggref->aggfilter != NULL)
	{
		appendStringInfoString(buf, ") FILTER (WHERE ");
		get_rule_expr((Node *) aggref->aggfilter, context, false);
	}

	appendStringInfoChar(buf, ')');
}

/*
 * This is a helper function for get_agg_expr().  It's used when we deparse
 * a combining Aggref; resolve_special_varno locates the corresponding partial
 * Aggref and then calls this.
 */
static void
get_agg_combine_expr(Node *node, deparse_context *context, void *callback_arg)
{
	Aggref	   *aggref;
	Aggref	   *original_aggref = callback_arg;

	if (!IsA(node, Aggref))
		elog(ERROR, "combining Aggref does not point to an Aggref");

	aggref = (Aggref *) node;
	get_agg_expr(aggref, context, original_aggref);
}

/*
 * get_windowfunc_expr	- Parse back a WindowFunc node
 */
static void
get_windowfunc_expr(WindowFunc *wfunc, deparse_context *context)
{
	StringInfo	buf = context->buf;
	Oid			argtypes[FUNC_MAX_ARGS];
	int			nargs;
	List	   *argnames;
	ListCell   *l;

	if (list_length(wfunc->args) > FUNC_MAX_ARGS)
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_ARGUMENTS),
				 errmsg("too many arguments")));
	nargs = 0;
	argnames = NIL;
	foreach(l, wfunc->args)
	{
		Node	   *arg = (Node *) lfirst(l);

		if (IsA(arg, NamedArgExpr))
			argnames = lappend(argnames, ((NamedArgExpr *) arg)->name);
		argtypes[nargs] = exprType(arg);
		nargs++;
	}

	appendStringInfo(buf, "%s(",
					 generate_function_name(wfunc->winfnoid, nargs,
											argnames, argtypes,
											false, NULL,
											context->special_exprkind));
	/* winstar can be set only in zero-argument aggregates */
	if (wfunc->winstar)
		appendStringInfoChar(buf, '*');
	else
		get_rule_expr((Node *) wfunc->args, context, true);

	if (wfunc->aggfilter != NULL)
	{
		appendStringInfoString(buf, ") FILTER (WHERE ");
		get_rule_expr((Node *) wfunc->aggfilter, context, false);
	}

	appendStringInfoString(buf, ") OVER ");

	foreach(l, context->windowClause)
	{
		WindowClause *wc = (WindowClause *) lfirst(l);

		if (wc->winref == wfunc->winref)
		{
			if (wc->name)
				appendStringInfoString(buf, quote_identifier(wc->name));
			else
				get_rule_windowspec(wc, context->windowTList, context);
			break;
		}
	}
	if (l == NULL)
	{
		if (context->windowClause)
			elog(ERROR, "could not find window clause for winref %u",
				 wfunc->winref);

		/*
		 * In EXPLAIN, we don't have window context information available, so
		 * we have to settle for this:
		 */
		appendStringInfoString(buf, "(?)");
	}
}


/*
 * helper for get_const_expr: append COLLATE if needed
 */
static void
get_const_collation(Const *constval, deparse_context *context)
{
	StringInfo	buf = context->buf;

	if (OidIsValid(constval->constcollid))
	{
		Oid			typcollation = get_typcollation(constval->consttype);

		if (constval->constcollid != typcollation)
		{
			appendStringInfo(buf, " COLLATE %s",
							 generate_collation_name(constval->constcollid));
		}
	}
}

/*
 * simple_quote_literal - Format a string as a SQL literal, append to buf
 */
static void
simple_quote_literal(StringInfo buf, const char *val)
{
	const char *valptr;

	/*
	 * We form the string literal according to the prevailing setting of
	 * standard_conforming_strings; we never use E''. User is responsible for
	 * making sure result is used correctly.
	 */
	appendStringInfoChar(buf, '\'');
	for (valptr = val; *valptr; valptr++)
	{
		char		ch = *valptr;

		if (SQL_STR_DOUBLE(ch, !standard_conforming_strings))
			appendStringInfoChar(buf, ch);
		appendStringInfoChar(buf, ch);
	}
	appendStringInfoChar(buf, '\'');
}

/* ----------
 * get_sublink_expr			- Parse back a sublink
 * ----------
 */
static void
get_sublink_expr(SubLink *sublink, deparse_context *context)
{
	StringInfo	buf = context->buf;
	Query	   *query = (Query *) (sublink->subselect);
	char	   *opname = NULL;
	bool		need_paren;

	if (sublink->subLinkType == ARRAY_SUBLINK)
		appendStringInfoString(buf, "ARRAY(");
	else
		appendStringInfoChar(buf, '(');

	/*
	 * Note that we print the name of only the first operator, when there are
	 * multiple combining operators.  This is an approximation that could go
	 * wrong in various scenarios (operators in different schemas, renamed
	 * operators, etc) but there is not a whole lot we can do about it, since
	 * the syntax allows only one operator to be shown.
	 */
	if (sublink->testexpr)
	{
		if (IsA(sublink->testexpr, OpExpr))
		{
			/* single combining operator */
			OpExpr	   *opexpr = (OpExpr *) sublink->testexpr;

			get_rule_expr(linitial(opexpr->args), context, true);
			opname = generate_operator_name(opexpr->opno,
											exprType(linitial(opexpr->args)),
											exprType(lsecond(opexpr->args)));
		}
		else if (IsA(sublink->testexpr, BoolExpr))
		{
			/* multiple combining operators, = or <> cases */
			char	   *sep;
			ListCell   *l;

			appendStringInfoChar(buf, '(');
			sep = "";
			foreach(l, ((BoolExpr *) sublink->testexpr)->args)
			{
				OpExpr	   *opexpr = lfirst_node(OpExpr, l);

				appendStringInfoString(buf, sep);
				get_rule_expr(linitial(opexpr->args), context, true);
				if (!opname)
					opname = generate_operator_name(opexpr->opno,
													exprType(linitial(opexpr->args)),
													exprType(lsecond(opexpr->args)));
				sep = ", ";
			}
			appendStringInfoChar(buf, ')');
		}
		else if (IsA(sublink->testexpr, RowCompareExpr))
		{
			/* multiple combining operators, < <= > >= cases */
			RowCompareExpr *rcexpr = (RowCompareExpr *) sublink->testexpr;

			appendStringInfoChar(buf, '(');
			get_rule_expr((Node *) rcexpr->largs, context, true);
			opname = generate_operator_name(linitial_oid(rcexpr->opnos),
											exprType(linitial(rcexpr->largs)),
											exprType(linitial(rcexpr->rargs)));
			appendStringInfoChar(buf, ')');
		}
		else
			elog(ERROR, "unrecognized testexpr type: %d",
				 (int) nodeTag(sublink->testexpr));
	}

	need_paren = true;

	switch (sublink->subLinkType)
	{
		case EXISTS_SUBLINK:
			appendStringInfoString(buf, "EXISTS ");
			break;

		case ANY_SUBLINK:
			if (strcmp(opname, "=") == 0)	/* Represent = ANY as IN */
				appendStringInfoString(buf, " IN ");
			else
				appendStringInfo(buf, " %s ANY ", opname);
			break;

		case ALL_SUBLINK:
			appendStringInfo(buf, " %s ALL ", opname);
			break;

		case ROWCOMPARE_SUBLINK:
			appendStringInfo(buf, " %s ", opname);
			break;

		case EXPR_SUBLINK:
		case MULTIEXPR_SUBLINK:
		case ARRAY_SUBLINK:
			need_paren = false;
			break;

		case CTE_SUBLINK:		/* shouldn't occur in a SubLink */
		default:
			elog(ERROR, "unrecognized sublink type: %d",
				 (int) sublink->subLinkType);
			break;
	}

	if (need_paren)
		appendStringInfoChar(buf, '(');

	get_query_def(query, buf, context->namespaces, NULL, false,
				  context->prettyFlags, context->wrapColumn,
				  context->indentLevel);

	if (need_paren)
		appendStringInfoString(buf, "))");
	else
		appendStringInfoChar(buf, ')');
}


/*
 * get_rule_expr_toplevel		- Parse back a toplevel expression
 *
 * Same as get_rule_expr(), except that if the expr is just a Var, we pass
 * istoplevel = true not false to get_variable().  This causes whole-row Vars
 * to get printed with decoration that will prevent expansion of "*".
 * We need to use this in contexts such as ROW() and VALUES(), where the
 * parser would expand "foo.*" appearing at top level.  (In principle we'd
 * use this in get_target_list() too, but that has additional worries about
 * whether to print AS, so it needs to invoke get_variable() directly anyway.)
 */
static void
get_rule_expr_toplevel(Node *node, deparse_context *context,
					   bool showimplicit)
{
	if (node && IsA(node, Var))
		(void) get_variable((Var *) node, 0, true, context);
	else
		get_rule_expr(node, context, showimplicit);
}

/*
 * get_rule_list_toplevel		- Parse back a list of toplevel expressions
 *
 * Apply get_rule_expr_toplevel() to each element of a List.
 *
 * This adds commas between the expressions, but caller is responsible
 * for printing surrounding decoration.
 */
static void
get_rule_list_toplevel(List *lst, deparse_context *context,
					   bool showimplicit)
{
	const char *sep;
	ListCell   *lc;

	sep = "";
	foreach(lc, lst)
	{
		Node	   *e = (Node *) lfirst(lc);

		appendStringInfoString(context->buf, sep);
		get_rule_expr_toplevel(e, context, showimplicit);
		sep = ", ";
	}
}

/*
 * get_rule_expr_funccall		- Parse back a function-call expression
 *
 * Same as get_rule_expr(), except that we guarantee that the output will
 * look like a function call, or like one of the things the grammar treats as
 * equivalent to a function call (see the func_expr_windowless production).
 * This is needed in places where the grammar uses func_expr_windowless and
 * you can't substitute a parenthesized a_expr.  If what we have isn't going
 * to look like a function call, wrap it in a dummy CAST() expression, which
 * will satisfy the grammar --- and, indeed, is likely what the user wrote to
 * produce such a thing.
 */
static void
get_rule_expr_funccall(Node *node, deparse_context *context,
					   bool showimplicit)
{
	if (looks_like_function(node))
		get_rule_expr(node, context, showimplicit);
	else
	{
		StringInfo	buf = context->buf;

		appendStringInfoString(buf, "CAST(");
		/* no point in showing any top-level implicit cast */
		get_rule_expr(node, context, false);
		appendStringInfo(buf, " AS %s)",
						 format_type_with_typemod(exprType(node),
												  exprTypmod(node)));
	}
}

/*
 * Helper function to identify node types that satisfy func_expr_windowless.
 * If in doubt, "false" is always a safe answer.
 */
static bool
looks_like_function(Node *node)
{
	if (node == NULL)
		return false;			/* probably shouldn't happen */
	switch (nodeTag(node))
	{
		case T_FuncExpr:
			/* OK, unless it's going to deparse as a cast */
			return (((FuncExpr *) node)->funcformat == COERCE_EXPLICIT_CALL ||
					((FuncExpr *) node)->funcformat == COERCE_SQL_SYNTAX);
		case T_NullIfExpr:
		case T_CoalesceExpr:
		case T_MinMaxExpr:
		case T_SQLValueFunction:
		case T_XmlExpr:
			/* these are all accepted by func_expr_common_subexpr */
			return true;
		default:
			break;
	}
	return false;
}

/*
 * get_oper_expr			- Parse back an OpExpr node
 */
static void
get_oper_expr(OpExpr *expr, deparse_context *context)
{
	StringInfo	buf = context->buf;
	Oid			opno = expr->opno;
	List	   *args = expr->args;

	if (!PRETTY_PAREN(context))
		appendStringInfoChar(buf, '(');
	if (list_length(args) == 2)
	{
		/* binary operator */
		Node	   *arg1 = (Node *) linitial(args);
		Node	   *arg2 = (Node *) lsecond(args);

		get_rule_expr_paren(arg1, context, true, (Node *) expr);
		appendStringInfo(buf, " %s ",
						 generate_operator_name(opno,
												exprType(arg1),
												exprType(arg2)));
		get_rule_expr_paren(arg2, context, true, (Node *) expr);
	}
	else
	{
		/* prefix operator */
		Node	   *arg = (Node *) linitial(args);

		appendStringInfo(buf, "%s ",
						 generate_operator_name(opno,
												InvalidOid,
												exprType(arg)));
		get_rule_expr_paren(arg, context, true, (Node *) expr);
	}
	if (!PRETTY_PAREN(context))
		appendStringInfoChar(buf, ')');
}

/*
 * get_func_expr			- Parse back a FuncExpr node
 */
static void
get_func_expr(FuncExpr *expr, deparse_context *context,
			  bool showimplicit)
{
	StringInfo	buf = context->buf;
	Oid			funcoid = expr->funcid;
	Oid			argtypes[FUNC_MAX_ARGS];
	int			nargs;
	List	   *argnames;
	bool		use_variadic;
	ListCell   *l;

	/*
	 * If the function call came from an implicit coercion, then just show the
	 * first argument --- unless caller wants to see implicit coercions.
	 */
	if (expr->funcformat == COERCE_IMPLICIT_CAST && !showimplicit)
	{
		get_rule_expr_paren((Node *) linitial(expr->args), context,
							false, (Node *) expr);
		return;
	}

	/*
	 * If the function call came from a cast, then show the first argument
	 * plus an explicit cast operation.
	 */
	if (expr->funcformat == COERCE_EXPLICIT_CAST ||
		expr->funcformat == COERCE_IMPLICIT_CAST)
	{
		Node	   *arg = linitial(expr->args);
		Oid			rettype = expr->funcresulttype;
		int32		coercedTypmod;

		/* Get the typmod if this is a length-coercion function */
		(void) exprIsLengthCoercion((Node *) expr, &coercedTypmod);

		get_coercion_expr(arg, context,
						  rettype, coercedTypmod,
						  (Node *) expr);

		return;
	}

	/*
	 * If the function was called using one of the SQL spec's random special
	 * syntaxes, try to reproduce that.  If we don't recognize the function,
	 * fall through.
	 */
	if (expr->funcformat == COERCE_SQL_SYNTAX)
	{
		if (get_func_sql_syntax(expr, context))
			return;
	}


	/*
	 * Normal function: display as proname(args).  First we need to extract
	 * the argument datatypes.
	 */
	if (list_length(expr->args) > FUNC_MAX_ARGS)
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_ARGUMENTS),
				 errmsg("too many arguments")));
	nargs = 0;
	argnames = NIL;
	foreach(l, expr->args)
	{
		Node	   *arg = (Node *) lfirst(l);

		if (IsA(arg, NamedArgExpr))
			argnames = lappend(argnames, ((NamedArgExpr *) arg)->name);
		argtypes[nargs] = exprType(arg);
		nargs++;
	}

	appendStringInfo(buf, "%s(",
					 generate_function_name(funcoid, nargs,
											argnames, argtypes,
											expr->funcvariadic,
											&use_variadic,
											context->special_exprkind));
	nargs = 0;
	foreach(l, expr->args)
	{
		if (nargs++ > 0)
			appendStringInfoString(buf, ", ");
		if (use_variadic && lnext(expr->args, l) == NULL)
			appendStringInfoString(buf, "VARIADIC ");
		get_rule_expr((Node *) lfirst(l), context, true);
	}

	appendStringInfoChar(buf, ')');
}

/*
 * has_dangerous_join_using: search jointree for unnamed JOIN USING
 *
 * Merged columns of a JOIN USING may act differently from either of the input
 * columns, either because they are merged with COALESCE (in a FULL JOIN) or
 * because an implicit coercion of the underlying input column is required.
 * In such a case the column must be referenced as a column of the JOIN not as
 * a column of either input.  And this is problematic if the join is unnamed
 * (alias-less): we cannot qualify the column's name with an RTE name, since
 * there is none.  (Forcibly assigning an alias to the join is not a solution,
 * since that will prevent legal references to tables below the join.)
 * To ensure that every column in the query is unambiguously referenceable,
 * we must assign such merged columns names that are globally unique across
 * the whole query, aliasing other columns out of the way as necessary.
 *
 * Because the ensuing re-aliasing is fairly damaging to the readability of
 * the query, we don't do this unless we have to.  So, we must pre-scan
 * the join tree to see if we have to, before starting set_using_names().
 */
static bool
has_dangerous_join_using(deparse_namespace *dpns, Node *jtnode)
{
	if (IsA(jtnode, RangeTblRef))
	{
		/* nothing to do here */
	}
	else if (IsA(jtnode, FromExpr))
	{
		FromExpr   *f = (FromExpr *) jtnode;
		ListCell   *lc;

		foreach(lc, f->fromlist)
		{
			if (has_dangerous_join_using(dpns, (Node *) lfirst(lc)))
				return true;
		}
	}
	else if (IsA(jtnode, JoinExpr))
	{
		JoinExpr   *j = (JoinExpr *) jtnode;

		/* Is it an unnamed JOIN with USING? */
		if (j->alias == NULL && j->usingClause)
		{
			/*
			 * Yes, so check each join alias var to see if any of them are not
			 * simple references to underlying columns.  If so, we have a
			 * dangerous situation and must pick unique aliases.
			 */
			RangeTblEntry *jrte = rt_fetch(j->rtindex, dpns->rtable);

			/* We need only examine the merged columns */
			for (int i = 0; i < jrte->joinmergedcols; i++)
			{
				Node	   *aliasvar = list_nth(jrte->joinaliasvars, i);

				if (!IsA(aliasvar, Var))
					return true;
			}
		}

		/* Nope, but inspect children */
		if (has_dangerous_join_using(dpns, j->larg))
			return true;
		if (has_dangerous_join_using(dpns, j->rarg))
			return true;
	}
	else
		elog(ERROR, "unrecognized node type: %d",
			 (int) nodeTag(jtnode));
	return false;
}

/*
 * set_using_names: select column aliases to be used for merged USING columns
 *
 * We do this during a recursive descent of the query jointree.
 * dpns->unique_using must already be set to determine the global strategy.
 *
 * Column alias info is saved in the dpns->rtable_columns list, which is
 * assumed to be filled with pre-zeroed deparse_columns structs.
 *
 * parentUsing is a list of all USING aliases assigned in parent joins of
 * the current jointree node.  (The passed-in list must not be modified.)
 */
static void
set_using_names(deparse_namespace *dpns, Node *jtnode, List *parentUsing)
{
	if (IsA(jtnode, RangeTblRef))
	{
		/* nothing to do now */
	}
	else if (IsA(jtnode, FromExpr))
	{
		FromExpr   *f = (FromExpr *) jtnode;
		ListCell   *lc;

		foreach(lc, f->fromlist)
			set_using_names(dpns, (Node *) lfirst(lc), parentUsing);
	}
	else if (IsA(jtnode, JoinExpr))
	{
		JoinExpr   *j = (JoinExpr *) jtnode;
		RangeTblEntry *rte = rt_fetch(j->rtindex, dpns->rtable);
		deparse_columns *colinfo = deparse_columns_fetch(j->rtindex, dpns);
		int		   *leftattnos;
		int		   *rightattnos;
		deparse_columns *leftcolinfo;
		deparse_columns *rightcolinfo;
		int			i;
		ListCell   *lc;

		/* Get info about the shape of the join */
		identify_join_columns(j, rte, colinfo);
		leftattnos = colinfo->leftattnos;
		rightattnos = colinfo->rightattnos;

		/* Look up the not-yet-filled-in child deparse_columns structs */
		leftcolinfo = deparse_columns_fetch(colinfo->leftrti, dpns);
		rightcolinfo = deparse_columns_fetch(colinfo->rightrti, dpns);

		/*
		 * If this join is unnamed, then we cannot substitute new aliases at
		 * this level, so any name requirements pushed down to here must be
		 * pushed down again to the children.
		 */
		if (rte->alias == NULL)
		{
			for (i = 0; i < colinfo->num_cols; i++)
			{
				char	   *colname = colinfo->colnames[i];

				if (colname == NULL)
					continue;

				/* Push down to left column, unless it's a system column */
				if (leftattnos[i] > 0)
				{
					expand_colnames_array_to(leftcolinfo, leftattnos[i]);
					leftcolinfo->colnames[leftattnos[i] - 1] = colname;
				}

				/* Same on the righthand side */
				if (rightattnos[i] > 0)
				{
					expand_colnames_array_to(rightcolinfo, rightattnos[i]);
					rightcolinfo->colnames[rightattnos[i] - 1] = colname;
				}
			}
		}

		/*
		 * If there's a USING clause, select the USING column names and push
		 * those names down to the children.  We have two strategies:
		 *
		 * If dpns->unique_using is true, we force all USING names to be
		 * unique across the whole query level.  In principle we'd only need
		 * the names of dangerous USING columns to be globally unique, but to
		 * safely assign all USING names in a single pass, we have to enforce
		 * the same uniqueness rule for all of them.  However, if a USING
		 * column's name has been pushed down from the parent, we should use
		 * it as-is rather than making a uniqueness adjustment.  This is
		 * necessary when we're at an unnamed join, and it creates no risk of
		 * ambiguity.  Also, if there's a user-written output alias for a
		 * merged column, we prefer to use that rather than the input name;
		 * this simplifies the logic and seems likely to lead to less aliasing
		 * overall.
		 *
		 * If dpns->unique_using is false, we only need USING names to be
		 * unique within their own join RTE.  We still need to honor
		 * pushed-down names, though.
		 *
		 * Though significantly different in results, these two strategies are
		 * implemented by the same code, with only the difference of whether
		 * to put assigned names into dpns->using_names.
		 */
		if (j->usingClause)
		{
			/* Copy the input parentUsing list so we don't modify it */
			parentUsing = list_copy(parentUsing);

			/* USING names must correspond to the first join output columns */
			expand_colnames_array_to(colinfo, list_length(j->usingClause));
			i = 0;
			foreach(lc, j->usingClause)
			{
				char	   *colname = strVal(lfirst(lc));

				/* Assert it's a merged column */
				Assert(leftattnos[i] != 0 && rightattnos[i] != 0);

				/* Adopt passed-down name if any, else select unique name */
				if (colinfo->colnames[i] != NULL)
					colname = colinfo->colnames[i];
				else
				{
					/* Prefer user-written output alias if any */
					if (rte->alias && i < list_length(rte->alias->colnames))
						colname = strVal(list_nth(rte->alias->colnames, i));
					/* Make it appropriately unique */
					colname = make_colname_unique(colname, dpns, colinfo);
					if (dpns->unique_using)
						dpns->using_names = lappend(dpns->using_names,
													colname);
					/* Save it as output column name, too */
					colinfo->colnames[i] = colname;
				}

				/* Remember selected names for use later */
				colinfo->usingNames = lappend(colinfo->usingNames, colname);
				parentUsing = lappend(parentUsing, colname);

				/* Push down to left column, unless it's a system column */
				if (leftattnos[i] > 0)
				{
					expand_colnames_array_to(leftcolinfo, leftattnos[i]);
					leftcolinfo->colnames[leftattnos[i] - 1] = colname;
				}

				/* Same on the righthand side */
				if (rightattnos[i] > 0)
				{
					expand_colnames_array_to(rightcolinfo, rightattnos[i]);
					rightcolinfo->colnames[rightattnos[i] - 1] = colname;
				}

				i++;
			}
		}

		/* Mark child deparse_columns structs with correct parentUsing info */
		leftcolinfo->parentUsing = parentUsing;
		rightcolinfo->parentUsing = parentUsing;

		/* Now recursively assign USING column names in children */
		set_using_names(dpns, j->larg, parentUsing);
		set_using_names(dpns, j->rarg, parentUsing);
	}
	else
		elog(ERROR, "unrecognized node type: %d",
			 (int) nodeTag(jtnode));
}


/*
 * get_agg_expr			- Parse back an Aggref node
 */
static void
get_agg_expr(Aggref *aggref, deparse_context *context,
			 Aggref *original_aggref)
{
	StringInfo	buf = context->buf;
	Oid			argtypes[FUNC_MAX_ARGS];
	int			nargs;
	bool		use_variadic;

	/*
	 * For a combining aggregate, we look up and deparse the corresponding
	 * partial aggregate instead.  This is necessary because our input
	 * argument list has been replaced; the new argument list always has just
	 * one element, which will point to a partial Aggref that supplies us with
	 * transition states to combine.
	 */
	if (DO_AGGSPLIT_COMBINE(aggref->aggsplit))
	{
		TargetEntry *tle;


		Assert(list_length(aggref->args) == 1);
		tle = linitial_node(TargetEntry, aggref->args);
		resolve_special_varno((Node *) tle->expr, context,
							  get_agg_combine_expr, original_aggref);
		return;
	}

	/*
	 * Mark as PARTIAL, if appropriate.  We look to the original aggref so as
	 * to avoid printing this when recursing from the code just above.
	 */
	if (DO_AGGSPLIT_SKIPFINAL(original_aggref->aggsplit))
		appendStringInfoString(buf, "PARTIAL ");

	/* Extract the argument types as seen by the parser */
	nargs = get_aggregate_argtypes(aggref, argtypes);

	/* Print the aggregate name, schema-qualified if needed */
	appendStringInfo(buf, "%s(%s",
					 generate_function_name(aggref->aggfnoid, nargs,
											NIL, argtypes,
											aggref->aggvariadic,
											&use_variadic,
											context->special_exprkind),
					 (aggref->aggdistinct != NIL) ? "DISTINCT " : "");

	if (AGGKIND_IS_ORDERED_SET(aggref->aggkind))
	{
		/*
		 * Ordered-set aggregates do not use "*" syntax.  Also, we needn't
		 * worry about inserting VARIADIC.  So we can just dump the direct
		 * args as-is.
		 */
		Assert(!aggref->aggvariadic);
		get_rule_expr((Node *) aggref->aggdirectargs, context, true);
		Assert(aggref->aggorder != NIL);
		appendStringInfoString(buf, ") WITHIN GROUP (ORDER BY ");
		get_rule_orderby(aggref->aggorder, aggref->args, false, context);
	}
	else
	{
		/* aggstar can be set only in zero-argument aggregates */
		if (aggref->aggstar)
			appendStringInfoChar(buf, '*');
		else
		{
			ListCell   *l;
			int			i;

			i = 0;
			foreach(l, aggref->args)
			{
				TargetEntry *tle = (TargetEntry *) lfirst(l);
				Node	   *arg = (Node *) tle->expr;

				Assert(!IsA(arg, NamedArgExpr));
				if (tle->resjunk)
					continue;
				if (i++ > 0)
					appendStringInfoString(buf, ", ");
				if (use_variadic && i == nargs)
					appendStringInfoString(buf, "VARIADIC ");
				get_rule_expr(arg, context, true);
			}
		}

		if (aggref->aggorder != NIL)
		{
			appendStringInfoString(buf, " ORDER BY ");
			get_rule_orderby(aggref->aggorder, aggref->args, false, context);
		}
	}

	if (aggref->aggfilter != NULL)
	{
		appendStringInfoString(buf, ") FILTER (WHERE ");
		get_rule_expr((Node *) aggref->aggfilter, context, false);
	}

	appendStringInfoChar(buf, ')');
}

/*
 * This is a helper function for get_agg_expr().  It's used when we deparse
 * a combining Aggref; resolve_special_varno locates the corresponding partial
 * Aggref and then calls this.
 */
static void
get_agg_combine_expr(Node *node, deparse_context *context, void *callback_arg)
{
	Aggref	   *aggref;
	Aggref	   *original_aggref = callback_arg;

	if (!IsA(node, Aggref))
		elog(ERROR, "combining Aggref does not point to an Aggref");

	aggref = (Aggref *) node;
	get_agg_expr(aggref, context, original_aggref);
}

/*
 * get_windowfunc_expr	- Parse back a WindowFunc node
 */
static void
get_windowfunc_expr(WindowFunc *wfunc, deparse_context *context)
{
	StringInfo	buf = context->buf;
	Oid			argtypes[FUNC_MAX_ARGS];
	int			nargs;
	List	   *argnames;
	ListCell   *l;

	if (list_length(wfunc->args) > FUNC_MAX_ARGS)
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_ARGUMENTS),
				 errmsg("too many arguments")));
	nargs = 0;
	argnames = NIL;
	foreach(l, wfunc->args)
	{
		Node	   *arg = (Node *) lfirst(l);

		if (IsA(arg, NamedArgExpr))
			argnames = lappend(argnames, ((NamedArgExpr *) arg)->name);
		argtypes[nargs] = exprType(arg);
		nargs++;
	}

	appendStringInfo(buf, "%s(",
					 generate_function_name(wfunc->winfnoid, nargs,
											argnames, argtypes,
											false, NULL,
											context->special_exprkind));
	/* winstar can be set only in zero-argument aggregates */
	if (wfunc->winstar)
		appendStringInfoChar(buf, '*');
	else
		get_rule_expr((Node *) wfunc->args, context, true);

	if (wfunc->aggfilter != NULL)
	{
		appendStringInfoString(buf, ") FILTER (WHERE ");
		get_rule_expr((Node *) wfunc->aggfilter, context, false);
	}

	appendStringInfoString(buf, ") OVER ");

	foreach(l, context->windowClause)
	{
		WindowClause *wc = (WindowClause *) lfirst(l);

		if (wc->winref == wfunc->winref)
		{
			if (wc->name)
				appendStringInfoString(buf, quote_identifier(wc->name));
			else
				get_rule_windowspec(wc, context->windowTList, context);
			break;
		}
	}
	if (l == NULL)
	{
		if (context->windowClause)
			elog(ERROR, "could not find window clause for winref %u",
				 wfunc->winref);

		/*
		 * In EXPLAIN, we don't have window context information available, so
		 * we have to settle for this:
		 */
		appendStringInfoString(buf, "(?)");
	}
}


/*
 * Deparse a Var which references OUTER_VAR, INNER_VAR, or INDEX_VAR.  This
 * routine is actually a callback for get_special_varno, which handles finding
 * the correct TargetEntry.  We get the expression contained in that
 * TargetEntry and just need to deparse it, a job we can throw back on
 * get_rule_expr.
 */
static void
get_special_variable(Node *node, deparse_context *context, void *callback_arg)
{
	StringInfo	buf = context->buf;

	/*
	 * For a non-Var referent, force parentheses because our caller probably
	 *  assumed a Var is a simple expression.
	 */
	if (!IsA(node, Var))
		appendStringInfoChar(buf, '(');
	get_rule_expr(node, context, true);
	if (!IsA(node, Var))
		appendStringInfoChar(buf, ')');
}

/*
 * Chase through plan references to special varnos (OUTER_VAR, INNER_VAR,
 * INDEX_VAR) until we find a real Var or some kind of non-Var node; then,
 * invoke the callback provided.
 */
static void
resolve_special_varno(Node *node, deparse_context *context, rsv_callback callback, void *callback_arg)
{
	Var		   *var;
	deparse_namespace *dpns;

	/* This function is recursive, so let's be paranoid. */
	check_stack_depth();

	/* If it's not a Var, invoke the callback. */
	if (!IsA(node, Var))
	{
		(*callback) (node, context, callback_arg);
		return;
	}

	/* Find appropriate nesting depth */
	var = (Var *) node;
	dpns = (deparse_namespace *) list_nth(context->namespaces,
										  var->varlevelsup);

	/*
	 * It's a special RTE, so recurse.
	 */
	if (var->varno == OUTER_VAR && dpns->outer_tlist)
	{
		TargetEntry *tle;
		deparse_namespace save_dpns;
		Bitmapset  *save_appendparents;

		tle = get_tle_by_resno(dpns->outer_tlist, var->varattno);
		if (!tle)
			elog(ERROR, "bogus varattno for OUTER_VAR var: %d", var->varattno);

		/* If we're descending to the first child of an Append or MergeAppend,
		 * update appendparents.  This will affect deparsing of all Vars
		 * appearing within the eventually-resolved subexpression.
		 */
		save_appendparents = context->appendparents;

		if (IsA(dpns->plan, Append))
			context->appendparents = bms_union(context->appendparents,
											   ((Append *) dpns->plan)->apprelids);
		else if (IsA(dpns->plan, MergeAppend))
			context->appendparents = bms_union(context->appendparents,
											   ((MergeAppend *) dpns->plan)->apprelids);

		push_child_plan(dpns, dpns->outer_plan, &save_dpns);
		resolve_special_varno((Node *) tle->expr, context,
							  callback, callback_arg);
		pop_child_plan(dpns, &save_dpns);
		context->appendparents = save_appendparents;
		return;
	}
	else if (var->varno == INNER_VAR && dpns->inner_tlist)
	{
		TargetEntry *tle;
		deparse_namespace save_dpns;

		tle = get_tle_by_resno(dpns->inner_tlist, var->varattno);
		if (!tle)
			elog(ERROR, "bogus varattno for INNER_VAR var: %d", var->varattno);

		push_child_plan(dpns, dpns->inner_plan, &save_dpns);
		resolve_special_varno((Node *) tle->expr, context, callback, callback_arg);
		pop_child_plan(dpns, &save_dpns);
		return;
	}
	else if (var->varno == INDEX_VAR && dpns->index_tlist)
	{
		TargetEntry *tle;

		tle = get_tle_by_resno(dpns->index_tlist, var->varattno);
		if (!tle)
			elog(ERROR, "bogus varattno for INDEX_VAR var: %d", var->varattno);

		resolve_special_varno((Node *) tle->expr, context, callback, callback_arg);
		return;
	}
	else if (var->varno < 1 || var->varno > list_length(dpns->rtable))
		elog(ERROR, "bogus varno: %d", var->varno);

	/* Not special.  Just invoke the callback. */
	(*callback) (node, context, callback_arg);
}

/*
 * get_rule_expr_funccall		- Parse back a function-call expression
 *
 * Same as get_rule_expr(), except that we guarantee that the output will
 * look like a function call, or like one of the things the grammar treats as
 * equivalent to a function call (see the func_expr_windowless production).
 * This is needed in places where the grammar uses func_expr_windowless and
 * you can't substitute a parenthesized a_expr.  If what we have isn't going
 * to look like a function call, wrap it in a dummy CAST() expression, which
 * will satisfy the grammar --- and, indeed, is likely what the user wrote to
 * produce such a thing.
 */
static void
get_rule_expr_funccall(Node *node, deparse_context *context,
					   bool showimplicit)
{
	if (looks_like_function(node))
		get_rule_expr(node, context, showimplicit);
	else
	{
		StringInfo	buf = context->buf;

		appendStringInfoString(buf, "CAST(");
		/* no point in showing any top-level implicit cast */
		get_rule_expr(node, context, false);
		appendStringInfo(buf, " AS %s)",
						 format_type_with_typemod(exprType(node),
												  exprTypmod(node)));
	}
}

/*
 * Helper function to identify node types that satisfy func_expr_windowless.
 * If in doubt, "false" is always a safe answer.
 */
static bool
looks_like_function(Node *node)
{
	if (node == NULL)
		return false;			/* probably shouldn't happen */
	switch (nodeTag(node))
	{
		case T_FuncExpr:
			/* OK, unless it's going to deparse as a cast */
			return (((FuncExpr *) node)->funcformat == COERCE_EXPLICIT_CALL ||
					((FuncExpr *) node)->funcformat == COERCE_SQL_SYNTAX);
		case T_NullIfExpr:
		case T_CoalesceExpr:
		case T_MinMaxExpr:
		case T_SQLValueFunction:
		case T_XmlExpr:
			/* these are all accepted by func_expr_common_subexpr */
			return true;
		default:
			break;
	}
	return false;
}


/*
 * get_oper_expr			- Parse back an OpExpr node
 */
static void
get_oper_expr(OpExpr *expr, deparse_context *context)
{
	StringInfo	buf = context->buf;
	Oid			opno = expr->opno;
	List	   *args = expr->args;

	if (!PRETTY_PAREN(context))
		appendStringInfoChar(buf, '(');
	if (list_length(args) == 2)
	{
		/* binary operator */
		Node	   *arg1 = (Node *) linitial(args);
		Node	   *arg2 = (Node *) lsecond(args);

		get_rule_expr_paren(arg1, context, true, (Node *) expr);
		appendStringInfo(buf, " %s ",
						 generate_operator_name(opno,
												exprType(arg1),
												exprType(arg2)));
		get_rule_expr_paren(arg2, context, true, (Node *) expr);
	}
	else
	{
		/* prefix operator */
		Node	   *arg = (Node *) linitial(args);

		appendStringInfo(buf, "%s ",
						 generate_operator_name(opno,
												InvalidOid,
												exprType(arg)));
		get_rule_expr_paren(arg, context, true, (Node *) expr);
	}
	if (!PRETTY_PAREN(context))
		appendStringInfoChar(buf, ')');
}

/*
 * get_func_expr			- Parse back a FuncExpr node
 */
static void
get_func_expr(FuncExpr *expr, deparse_context *context,
			  bool showimplicit)
{
	StringInfo	buf = context->buf;
	Oid			funcoid = expr->funcid;
	Oid			argtypes[FUNC_MAX_ARGS];
	int			nargs;
	List	   *argnames;
	bool		use_variadic;
	ListCell   *l;

	/*
	 * If the function call came from an implicit coercion, then just show the
	 * first argument --- unless caller wants to see implicit coercions.
	 */
	if (expr->funcformat == COERCE_IMPLICIT_CAST && !showimplicit)
	{
		get_rule_expr_paren((Node *) linitial(expr->args), context,
							false, (Node *) expr);
		return;
	}

	/*
	 * If the function call came from a cast, then show the first argument
	 * plus an explicit cast operation.
	 */
	if (expr->funcformat == COERCE_EXPLICIT_CAST ||
		expr->funcformat == COERCE_IMPLICIT_CAST)
	{
		Node	   *arg = linitial(expr->args);
		Oid			rettype = expr->funcresulttype;
		int32		coercedTypmod;

		/* Get the typmod if this is a length-coercion function */
		(void) exprIsLengthCoercion((Node *) expr, &coercedTypmod);

		get_coercion_expr(arg, context,
						  rettype, coercedTypmod,
						  (Node *) expr);

		return;
	}

	/*
	 * If the function was called using one of the SQL spec's random special
	 * syntaxes, try to reproduce that.  If we don't recognize the function,
	 * fall through.
	 */
	if (expr->funcformat == COERCE_SQL_SYNTAX)
	{
		if (get_func_sql_syntax(expr, context))
			return;
	}


	/*
	 * Normal function: display as proname(args).  First we need to extract
	 * the argument datatypes.
	 */
	if (list_length(expr->args) > FUNC_MAX_ARGS)
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_ARGUMENTS),
				 errmsg("too many arguments")));
	nargs = 0;
	argnames = NIL;
	foreach(l, expr->args)
	{
		Node	   *arg = (Node *) lfirst(l);

		if (IsA(arg, NamedArgExpr))
			argnames = lappend(argnames, ((NamedArgExpr *) arg)->name);
		argtypes[nargs] = exprType(arg);
		nargs++;
	}

	appendStringInfo(buf, "%s(",
					 generate_function_name(funcoid, nargs,
											argnames, argtypes,
											expr->funcvariadic,
											&use_variadic,
											context->special_exprkind));
	nargs = 0;
	foreach(l, expr->args)
	{
		if (nargs++ > 0)
			appendStringInfoString(buf, ", ");
		if (use_variadic && lnext(expr->args, l) == NULL)
			appendStringInfoString(buf, "VARIADIC ");
		get_rule_expr((Node *) lfirst(l), context, true);
	}

	appendStringInfoChar(buf, ')');
}

/*
 * set_rtable_names: select RTE aliases to be used in printing a query
 *
 * We fill in dpns->rtable_names with a list of names that is one-for-one with
 * the already-filled dpns->rtable list.  Each RTE name is unique among those
 * in the new namespace plus any ancestor namespaces listed in
 * parent_namespaces.
 *
 * If rels_used isn't NULL, only RTE indexes listed in it are given aliases.
 *
 * Note that this function is only concerned with relation names, not column
 * names.
 */
static void
set_rtable_names(deparse_namespace *dpns, List *parent_namespaces,
				 Bitmapset *rels_used)
{
	HASHCTL		hash_ctl;
	HTAB	   *names_hash;
	NameHashEntry *hentry;
	bool		found;
	int			rtindex;
	ListCell   *lc;

	dpns->rtable_names = NIL;
	/* nothing more to do if empty rtable */
	if (dpns->rtable == NIL)
		return;

	/*
	 * We use a hash table to hold known names, so that this process is O(N)
	 * not O(N^2) for N names.
	 */
	hash_ctl.keysize = NAMEDATALEN;
	hash_ctl.entrysize = sizeof(NameHashEntry);
	hash_ctl.hcxt = CurrentMemoryContext;
	names_hash = hash_create("set_rtable_names names",
							 list_length(dpns->rtable),
							 &hash_ctl,
							 HASH_ELEM | HASH_STRINGS | HASH_CONTEXT);

	/* Preload the hash table with names appearing in parent_namespaces */
	foreach(lc, parent_namespaces)
	{
		deparse_namespace *olddpns = (deparse_namespace *) lfirst(lc);
		ListCell   *lc2;

		foreach(lc2, olddpns->rtable_names)
		{
			char	   *oldname = (char *) lfirst(lc2);

			if (oldname == NULL)
				continue;
			hentry = (NameHashEntry *) hash_search(names_hash,
												   oldname,
												   HASH_ENTER,
												   &found);
			/* we do not complain about duplicate names in parent namespaces */
			hentry->counter = 0;
		}
	}

	/* Now we can scan the rtable */
	rtindex = 1;
	foreach(lc, dpns->rtable)
	{
		RangeTblEntry *rte = (RangeTblEntry *) lfirst(lc);
		char	   *refname;

		/* Just in case this takes an unreasonable amount of time ... */
		CHECK_FOR_INTERRUPTS();

		if (rels_used && !bms_is_member(rtindex, rels_used))
		{
			/* Ignore unreferenced RTE */
			refname = NULL;
		}
		else if (rte->alias)
		{
			/* If RTE has a user-defined alias, prefer that */
			refname = rte->alias->aliasname;
		}
		else if (rte->rtekind == RTE_RELATION)
		{
			/* Use the current actual name of the relation */
			refname = get_rel_name(rte->relid);
		}
		else if (rte->rtekind == RTE_JOIN)
		{
			/* Unnamed join has no refname */
			refname = NULL;
		}
		else
		{
			/* Otherwise use whatever the parser assigned */
			refname = rte->eref->aliasname;
		}

		/*
		 * If the selected name isn't unique, append digits to make it so, and
		 * make a new hash entry for it once we've got a unique name.  For a
		 * very long input name, we might have to truncate to stay within
		 * NAMEDATALEN.
		 */
		if (refname)
		{
			hentry = (NameHashEntry *) hash_search(names_hash,
												   refname,
												   HASH_ENTER,
												   &found);
			if (found)
			{
				/* Name already in use, must choose a new one */
				int			refnamelen = strlen(refname);
				char	   *modname = (char *) palloc(refnamelen + 16);
				NameHashEntry *hentry2;

				do
				{
					hentry->counter++;
					for (;;)
					{
						memcpy(modname, refname, refnamelen);
						sprintf(modname + refnamelen, "_%d", hentry->counter);
						if (strlen(modname) < NAMEDATALEN)
							break;
						/* drop chars from refname to keep all the digits */
						refnamelen = pg_mbcliplen(refname, refnamelen,
												  refnamelen - 1);
					}
					hentry2 = (NameHashEntry *) hash_search(names_hash,
															modname,
															HASH_ENTER,
															&found);
				} while (found);
				hentry2->counter = 0;	/* init new hash entry */
				refname = modname;
			}
			else
			{
				/* Name not previously used, need only initialize hentry */
				hentry->counter = 0;
			}
		}

		dpns->rtable_names = lappend(dpns->rtable_names, refname);
		rtindex++;
	}

	hash_destroy(names_hash);
}

/*
 * set_deparse_for_query: set up deparse_namespace for deparsing a Query tree
 *
 * For convenience, this is defined to initialize the deparse_namespace struct
 * from scratch.
 */
static void
set_deparse_for_query(deparse_namespace *dpns, Query *query,
					  List *parent_namespaces)
{
	ListCell   *lc;
	ListCell   *lc2;

	/* Initialize *dpns and fill rtable/ctes links */
	memset(dpns, 0, sizeof(deparse_namespace));
	dpns->rtable = query->rtable;
	dpns->subplans = NIL;
	dpns->ctes = query->cteList;
	dpns->appendrels = NULL;

	/* Assign a unique relation alias to each RTE */
	set_rtable_names(dpns, parent_namespaces, NULL);

	/* Initialize dpns->rtable_columns to contain zeroed structs */
	dpns->rtable_columns = NIL;
	while (list_length(dpns->rtable_columns) < list_length(dpns->rtable))
		dpns->rtable_columns = lappend(dpns->rtable_columns,
									   palloc0(sizeof(deparse_columns)));

	/* If it's a utility query, it won't have a jointree */
	if (query->jointree)
	{
		/* Detect whether global uniqueness of USING names is needed */
		dpns->unique_using =
			has_dangerous_join_using(dpns, (Node *) query->jointree);

		/*
		 * Select names for columns merged by USING, via a recursive pass over
		 * the query jointree.
		 */
		set_using_names(dpns, (Node *) query->jointree, NIL);
	}

	/*
	 * Now assign remaining column aliases for each RTE.  We do this in a
	 * linear scan of the rtable, so as to process RTEs whether or not they
	 * are in the jointree (we mustn't miss NEW.*, INSERT target relations,
	 * etc).  JOIN RTEs must be processed after their children, but this is
	 * okay because they appear later in the rtable list than their children
	 * (cf Asserts in identify_join_columns()).
	 */
	forboth(lc, dpns->rtable, lc2, dpns->rtable_columns)
	{
		RangeTblEntry *rte = (RangeTblEntry *) lfirst(lc);
		deparse_columns *colinfo = (deparse_columns *) lfirst(lc2);

		if (rte->rtekind == RTE_JOIN)
			set_join_column_names(dpns, rte, colinfo);
		else
			set_relation_column_names(dpns, rte, colinfo);
	}
}

/* ----------
 * get_update_query_targetlist_def			- Parse back an UPDATE targetlist
 * ----------
 */
static void
get_update_query_targetlist_def(Query *query, List *targetList,
								deparse_context *context, RangeTblEntry *rte)
{
	StringInfo	buf = context->buf;
	ListCell   *l;
	ListCell   *next_ma_cell;
	int			remaining_ma_columns;
	const char *sep;
	SubLink    *cur_ma_sublink;
	List	   *ma_sublinks;

	/*
	 * Prepare to deal with MULTIEXPR assignments: collect the source SubLinks
	 * into a list.  We expect them to appear, in ID order, in resjunk tlist
	 * entries.
	 */
	ma_sublinks = NIL;
	if (query->hasSubLinks)		/* else there can't be any */
	{
		foreach(l, targetList)
		{
			TargetEntry *tle = (TargetEntry *) lfirst(l);

			if (tle->resjunk && IsA(tle->expr, SubLink))
			{
				SubLink    *sl = (SubLink *) tle->expr;

				if (sl->subLinkType == MULTIEXPR_SUBLINK)
				{
					ma_sublinks = lappend(ma_sublinks, sl);
					Assert(sl->subLinkId == list_length(ma_sublinks));
				}
			}
		}
	}
	next_ma_cell = list_head(ma_sublinks);
	cur_ma_sublink = NULL;
	remaining_ma_columns = 0;

	/* Add the comma separated list of 'attname = value' */
	sep = "";
	foreach(l, targetList)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(l);
		Node	   *expr;

		if (tle->resjunk)
			continue;			/* ignore junk entries */

		/* Emit separator (OK whether we're in multiassignment or not) */
		appendStringInfoString(buf, sep);
		sep = ", ";

		/*
		 * Check to see if we're starting a multiassignment group: if so,
		 * output a left paren.
		 */
		if (next_ma_cell != NULL && cur_ma_sublink == NULL)
		{
			/*
			 * We must dig down into the expr to see if it's a PARAM_MULTIEXPR
			 * Param.  That could be buried under FieldStores and
			 * SubscriptingRefs and CoerceToDomains (cf processIndirection()),
			 * and underneath those there could be an implicit type coercion.
			 * Because we would ignore implicit type coercions anyway, we
			 * don't need to be as careful as processIndirection() is about
			 * descending past implicit CoerceToDomains.
			 */
			expr = (Node *) tle->expr;
			while (expr)
			{
				if (IsA(expr, FieldStore))
				{
					FieldStore *fstore = (FieldStore *) expr;

					expr = (Node *) linitial(fstore->newvals);
				}
				else if (IsA(expr, SubscriptingRef))
				{
					SubscriptingRef *sbsref = (SubscriptingRef *) expr;

					if (sbsref->refassgnexpr == NULL)
						break;
					expr = (Node *) sbsref->refassgnexpr;
				}
				else if (IsA(expr, CoerceToDomain))
				{
					CoerceToDomain *cdomain = (CoerceToDomain *) expr;

					if (cdomain->coercionformat != COERCE_IMPLICIT_CAST)
						break;
					expr = (Node *) cdomain->arg;
				}
				else
					break;
			}
			expr = strip_implicit_coercions(expr);

			if (expr && IsA(expr, Param) &&
				((Param *) expr)->paramkind == PARAM_MULTIEXPR)
			{
				cur_ma_sublink = (SubLink *) lfirst(next_ma_cell);
				next_ma_cell = lnext(ma_sublinks, next_ma_cell);
				remaining_ma_columns = count_nonjunk_tlist_entries(
																   ((Query *) cur_ma_sublink->subselect)->targetList);
				Assert(((Param *) expr)->paramid ==
					   ((cur_ma_sublink->subLinkId << 16) | 1));
				appendStringInfoChar(buf, '(');
			}
		}

		/*
		 * Put out name of target column; look in the catalogs, not at
		 * tle->resname, since resname will fail to track RENAME.
		 */
		appendStringInfoString(buf,
							   quote_identifier(get_attname(rte->relid,
															tle->resno,
															false)));

		/*
		 * Print any indirection needed (subfields or subscripts), and strip
		 * off the top-level nodes representing the indirection assignments.
		 */
		expr = processIndirection((Node *) tle->expr, context);

		/*
		 * If we're in a multiassignment, skip printing anything more, unless
		 * this is the last column; in which case, what we print should be the
		 * sublink, not the Param.
		 */
		if (cur_ma_sublink != NULL)
		{
			if (--remaining_ma_columns > 0)
				continue;		/* not the last column of multiassignment */
			appendStringInfoChar(buf, ')');
			expr = (Node *) cur_ma_sublink;
			cur_ma_sublink = NULL;
		}

		appendStringInfoString(buf, " = ");

		get_rule_expr(expr, context, false);
	}
}

/*
 * get_column_alias_list - print column alias list for an RTE
 *
 * Caller must already have printed the relation's alias name.
 */
static void
get_column_alias_list(deparse_columns *colinfo, deparse_context *context)
{
	StringInfo	buf = context->buf;
	int			i;
	bool		first = true;

	/* Don't print aliases if not needed */
	if (!colinfo->printaliases)
		return;

	for (i = 0; i < colinfo->num_new_cols; i++)
	{
		char	   *colname = colinfo->new_colnames[i];

		if (first)
		{
			appendStringInfoChar(buf, '(');
			first = false;
		}
		else
			appendStringInfoString(buf, ", ");
		appendStringInfoString(buf, quote_identifier(colname));
	}
	if (!first)
		appendStringInfoChar(buf, ')');
}

/*
 * get_from_clause_coldeflist - reproduce FROM clause coldeflist
 *
 * When printing a top-level coldeflist (which is syntactically also the
 * relation's column alias list), use column names from colinfo.  But when
 * printing a coldeflist embedded inside ROWS FROM(), we prefer to use the
 * original coldeflist's names, which are available in rtfunc->funccolnames.
 * Pass NULL for colinfo to select the latter behavior.
 *
 * The coldeflist is appended immediately (no space) to buf.  Caller is
 * responsible for ensuring that an alias or AS is present before it.
 */
static void
get_from_clause_coldeflist(RangeTblFunction *rtfunc,
						   deparse_columns *colinfo,
						   deparse_context *context)
{
	StringInfo	buf = context->buf;
	ListCell   *l1;
	ListCell   *l2;
	ListCell   *l3;
	ListCell   *l4;
	int			i;

	appendStringInfoChar(buf, '(');

	i = 0;
	forfour(l1, rtfunc->funccoltypes,
			l2, rtfunc->funccoltypmods,
			l3, rtfunc->funccolcollations,
			l4, rtfunc->funccolnames)
	{
		Oid			atttypid = lfirst_oid(l1);
		int32		atttypmod = lfirst_int(l2);
		Oid			attcollation = lfirst_oid(l3);
		char	   *attname;

		if (colinfo)
			attname = colinfo->colnames[i];
		else
			attname = strVal(lfirst(l4));

		Assert(attname);		/* shouldn't be any dropped columns here */

		if (i > 0)
			appendStringInfoString(buf, ", ");
		appendStringInfo(buf, "%s %s",
						 quote_identifier(attname),
						 format_type_with_typemod(atttypid, atttypmod));
		if (OidIsValid(attcollation) &&
			attcollation != get_typcollation(atttypid))
			appendStringInfo(buf, " COLLATE %s",
							 generate_collation_name(attcollation));

		i++;
	}

	appendStringInfoChar(buf, ')');
}

/*
 * get_tablesample_def			- print a TableSampleClause
 */
static void
get_tablesample_def(TableSampleClause *tablesample, deparse_context *context)
{
	StringInfo	buf = context->buf;
	Oid			argtypes[1];
	int			nargs;
	ListCell   *l;

	/*
	 * We should qualify the handler's function name if it wouldn't be
	 * resolved by lookup in the current search path.
	 */
	argtypes[0] = INTERNALOID;
	appendStringInfo(buf, " TABLESAMPLE %s (",
					 generate_function_name(tablesample->tsmhandler, 1,
											NIL, argtypes,
											false, NULL, EXPR_KIND_NONE));

	nargs = 0;
	foreach(l, tablesample->args)
	{
		if (nargs++ > 0)
			appendStringInfoString(buf, ", ");
		get_rule_expr((Node *) lfirst(l), context, false);
	}
	appendStringInfoChar(buf, ')');

	if (tablesample->repeatable != NULL)
	{
		appendStringInfoString(buf, " REPEATABLE (");
		get_rule_expr((Node *) tablesample->repeatable, context, false);
		appendStringInfoChar(buf, ')');
	}
}

/*
 * get_opclass_name			- fetch name of an index operator class
 *
 * The opclass name is appended (after a space) to buf.
 *
 * Output is suppressed if the opclass is the default for the given
 * actual_datatype.  (If you don't want this behavior, just pass
 * InvalidOid for actual_datatype.)
 */
static void
get_opclass_name(Oid opclass, Oid actual_datatype,
				 StringInfo buf)
{
	HeapTuple	ht_opc;
	Form_pg_opclass opcrec;
	char	   *opcname;
	char	   *nspname;

	ht_opc = SearchSysCache1(CLAOID, ObjectIdGetDatum(opclass));
	if (!HeapTupleIsValid(ht_opc))
		elog(ERROR, "cache lookup failed for opclass %u", opclass);
	opcrec = (Form_pg_opclass) GETSTRUCT(ht_opc);

	if (!OidIsValid(actual_datatype) ||
		GetDefaultOpClass(actual_datatype, opcrec->opcmethod) != opclass)
	{
		/* Okay, we need the opclass name.  Do we need to qualify it? */
		opcname = NameStr(opcrec->opcname);
		if (OpclassIsVisible(opclass))
			appendStringInfo(buf, " %s", quote_identifier(opcname));
		else
		{
			nspname = get_namespace_name_or_temp(opcrec->opcnamespace);
			appendStringInfo(buf, " %s.%s",
							 quote_identifier(nspname),
							 quote_identifier(opcname));
		}
	}
	ReleaseSysCache(ht_opc);
}

/*
 * set_deparse_for_query: set up deparse_namespace for deparsing a Query tree
 *
 * For convenience, this is defined to initialize the deparse_namespace struct
 * from scratch.
 */
static void
set_deparse_for_query(deparse_namespace *dpns, Query *query,
					  List *parent_namespaces)
{
	ListCell   *lc;
	ListCell   *lc2;

	/* Initialize *dpns and fill rtable/ctes links */
	memset(dpns, 0, sizeof(deparse_namespace));
	dpns->rtable = query->rtable;
	dpns->subplans = NIL;
	dpns->ctes = query->cteList;
	dpns->appendrels = NULL;

	/* Assign a unique relation alias to each RTE */
	set_rtable_names(dpns, parent_namespaces, NULL);

	/* Initialize dpns->rtable_columns to contain zeroed structs */
	dpns->rtable_columns = NIL;
	while (list_length(dpns->rtable_columns) < list_length(dpns->rtable))
		dpns->rtable_columns = lappend(dpns->rtable_columns,
									   palloc0(sizeof(deparse_columns)));

	/* If it's a utility query, it won't have a jointree */
	if (query->jointree)
	{
		/* Detect whether global uniqueness of USING names is needed */
		dpns->unique_using =
			has_dangerous_join_using(dpns, (Node *) query->jointree);

		/*
		 * Select names for columns merged by USING, via a recursive pass over
		 * the query jointree.
		 */
		set_using_names(dpns, (Node *) query->jointree, NIL);
	}

	/*
	 * Now assign remaining column aliases for each RTE.  We do this in a
	 * linear scan of the rtable, so as to process RTEs whether or not they
	 * are in the jointree (we mustn't miss NEW.*, INSERT target relations,
	 * etc).  JOIN RTEs must be processed after their children, but this is
	 * okay because they appear later in the rtable list than their children
	 * (cf Asserts in identify_join_columns()).
	 */
	forboth(lc, dpns->rtable, lc2, dpns->rtable_columns)
	{
		RangeTblEntry *rte = (RangeTblEntry *) lfirst(lc);
		deparse_columns *colinfo = (deparse_columns *) lfirst(lc2);

		if (rte->rtekind == RTE_JOIN)
			set_join_column_names(dpns, rte, colinfo);
		else
			set_relation_column_names(dpns, rte, colinfo);
	}
}

/*
 * get_column_alias_list - print column alias list for an RTE
 *
 * Caller must already have printed the relation's alias name.
 */
static void
get_column_alias_list(deparse_columns *colinfo, deparse_context *context)
{
	StringInfo	buf = context->buf;
	int			i;
	bool		first = true;

	/* Don't print aliases if not needed */
	if (!colinfo->printaliases)
		return;

	for (i = 0; i < colinfo->num_new_cols; i++)
	{
		char	   *colname = colinfo->new_colnames[i];

		if (first)
		{
			appendStringInfoChar(buf, '(');
			first = false;
		}
		else
			appendStringInfoString(buf, ", ");
		appendStringInfoString(buf, quote_identifier(colname));
	}
	if (!first)
		appendStringInfoChar(buf, ')');
}

/*
 * get_from_clause_coldeflist - reproduce FROM clause coldeflist
 *
 * When printing a top-level coldeflist (which is syntactically also the
 * relation's column alias list), use column names from colinfo.  But when
 * printing a coldeflist embedded inside ROWS FROM(), we prefer to use the
 * original coldeflist's names, which are available in rtfunc->funccolnames.
 * Pass NULL for colinfo to select the latter behavior.
 *
 * The coldeflist is appended immediately (no space) to buf.  Caller is
 * responsible for ensuring that an alias or AS is present before it.
 */
static void
get_from_clause_coldeflist(RangeTblFunction *rtfunc,
						   deparse_columns *colinfo,
						   deparse_context *context)
{
	StringInfo	buf = context->buf;
	ListCell   *l1;
	ListCell   *l2;
	ListCell   *l3;
	ListCell   *l4;
	int			i;

	appendStringInfoChar(buf, '(');

	i = 0;
	forfour(l1, rtfunc->funccoltypes,
			l2, rtfunc->funccoltypmods,
			l3, rtfunc->funccolcollations,
			l4, rtfunc->funccolnames)
	{
		Oid			atttypid = lfirst_oid(l1);
		int32		atttypmod = lfirst_int(l2);
		Oid			attcollation = lfirst_oid(l3);
		char	   *attname;

		if (colinfo)
			attname = colinfo->colnames[i];
		else
			attname = strVal(lfirst(l4));

		Assert(attname);		/* shouldn't be any dropped columns here */

		if (i > 0)
			appendStringInfoString(buf, ", ");
		appendStringInfo(buf, "%s %s",
						 quote_identifier(attname),
						 format_type_with_typemod(atttypid, atttypmod));
		if (OidIsValid(attcollation) &&
			attcollation != get_typcollation(atttypid))
			appendStringInfo(buf, " COLLATE %s",
							 generate_collation_name(attcollation));

		i++;
	}

	appendStringInfoChar(buf, ')');
}

/*
 * get_tablesample_def			- print a TableSampleClause
 */
static void
get_tablesample_def(TableSampleClause *tablesample, deparse_context *context)
{
	StringInfo	buf = context->buf;
	Oid			argtypes[1];
	int			nargs;
	ListCell   *l;

	/*
	 * We should qualify the handler's function name if it wouldn't be
	 * resolved by lookup in the current search path.
	 */
	argtypes[0] = INTERNALOID;
	appendStringInfo(buf, " TABLESAMPLE %s (",
					 generate_function_name(tablesample->tsmhandler, 1,
											NIL, argtypes,
											false, NULL, EXPR_KIND_NONE));

	nargs = 0;
	foreach(l, tablesample->args)
	{
		if (nargs++ > 0)
			appendStringInfoString(buf, ", ");
		get_rule_expr((Node *) lfirst(l), context, false);
	}
	appendStringInfoChar(buf, ')');

	if (tablesample->repeatable != NULL)
	{
		appendStringInfoString(buf, " REPEATABLE (");
		get_rule_expr((Node *) tablesample->repeatable, context, false);
		appendStringInfoChar(buf, ')');
	}
}

/*
 * get_simple_binary_op_name
 *
 * helper function for isSimpleNode
 * will return single char binary operator name, or NULL if it's not
 */
static const char *
get_simple_binary_op_name(OpExpr *expr)
{
	List	   *args = expr->args;

	if (list_length(args) == 2)
	{
		/* binary operator */
		Node	   *arg1 = (Node *) linitial(args);
		Node	   *arg2 = (Node *) lsecond(args);
		const char *op;

		op = generate_operator_name(expr->opno, exprType(arg1), exprType(arg2));
		if (strlen(op) == 1)
			return op;
	}
	return NULL;
}

/*
 * Deparse a Var which references OUTER_VAR, INNER_VAR, or INDEX_VAR.  This
 * routine is actually a callback for get_special_varno, which handles finding
 * the correct TargetEntry.  We get the expression contained in that
 * TargetEntry and just need to deparse it, a job we can throw back on
 * get_rule_expr.
 */
static void
get_special_variable(Node *node, deparse_context *context, void *callback_arg)
{
	StringInfo	buf = context->buf;

	/*
	 * For a non-Var referent, force parentheses because our caller probably
	 *  assumed a Var is a simple expression.
	 */
	if (!IsA(node, Var))
		appendStringInfoChar(buf, '(');
	get_rule_expr(node, context, true);
	if (!IsA(node, Var))
		appendStringInfoChar(buf, ')');
}

static void
resolve_special_varno(Node *node, deparse_context *context, rsv_callback callback, void *callback_arg)
{
	Var		   *var;
	deparse_namespace *dpns;

	/* This function is recursive, so let's be paranoid. */
	check_stack_depth();

	/* If it's not a Var, invoke the callback. */
	if (!IsA(node, Var))
	{
		(*callback) (node, context, callback_arg);
		return;
	}

	/* Find appropriate nesting depth */
	var = (Var *) node;
	dpns = (deparse_namespace *) list_nth(context->namespaces,
										  var->varlevelsup);

	/*
	 * It's a special RTE, so recurse.
	 */
	if (var->varno == OUTER_VAR && dpns->outer_tlist)
	{
		TargetEntry *tle;
		deparse_namespace save_dpns;
		Bitmapset  *save_appendparents;

		tle = get_tle_by_resno(dpns->outer_tlist, var->varattno);
		if (!tle)
			elog(ERROR, "bogus varattno for OUTER_VAR var: %d", var->varattno);

		/* If we're descending to the first child of an Append or MergeAppend,
		 * update appendparents.  This will affect deparsing of all Vars
		 * appearing within the eventually-resolved subexpression.
		 */
		save_appendparents = context->appendparents;

		if (IsA(dpns->plan, Append))
			context->appendparents = bms_union(context->appendparents,
											   ((Append *) dpns->plan)->apprelids);
		else if (IsA(dpns->plan, MergeAppend))
			context->appendparents = bms_union(context->appendparents,
											   ((MergeAppend *) dpns->plan)->apprelids);

		push_child_plan(dpns, dpns->outer_plan, &save_dpns);
		resolve_special_varno((Node *) tle->expr, context,
							  callback, callback_arg);
		pop_child_plan(dpns, &save_dpns);
		context->appendparents = save_appendparents;
		return;
	}
	else if (var->varno == INNER_VAR && dpns->inner_tlist)
	{
		TargetEntry *tle;
		deparse_namespace save_dpns;

		tle = get_tle_by_resno(dpns->inner_tlist, var->varattno);
		if (!tle)
			elog(ERROR, "bogus varattno for INNER_VAR var: %d", var->varattno);

		push_child_plan(dpns, dpns->inner_plan, &save_dpns);
		resolve_special_varno((Node *) tle->expr, context, callback, callback_arg);
		pop_child_plan(dpns, &save_dpns);
		return;
	}
	else if (var->varno == INDEX_VAR && dpns->index_tlist)
	{
		TargetEntry *tle;

		tle = get_tle_by_resno(dpns->index_tlist, var->varattno);
		if (!tle)
			elog(ERROR, "bogus varattno for INDEX_VAR var: %d", var->varattno);

		resolve_special_varno((Node *) tle->expr, context, callback, callback_arg);
		return;
	}
	else if (var->varno < 1 || var->varno > list_length(dpns->rtable))
		elog(ERROR, "bogus varno: %d", var->varno);

	/* Not special.  Just invoke the callback. */
	(*callback) (node, context, callback_arg);
}


static void
get_update_query_targetlist_def(Query *query, List *targetList,
								deparse_context *context, RangeTblEntry *rte)
{
	StringInfo	buf = context->buf;
	ListCell   *l;
	ListCell   *next_ma_cell;
	int			remaining_ma_columns;
	const char *sep;
	SubLink    *cur_ma_sublink;
	List	   *ma_sublinks;

	/*
	 * Prepare to deal with MULTIEXPR assignments: collect the source SubLinks
	 * into a list.  We expect them to appear, in ID order, in resjunk tlist
	 * entries.
	 */
	ma_sublinks = NIL;
	if (query->hasSubLinks)		/* else there can't be any */
	{
		foreach(l, targetList)
		{
			TargetEntry *tle = (TargetEntry *) lfirst(l);

			if (tle->resjunk && IsA(tle->expr, SubLink))
			{
				SubLink    *sl = (SubLink *) tle->expr;

				if (sl->subLinkType == MULTIEXPR_SUBLINK)
				{
					ma_sublinks = lappend(ma_sublinks, sl);
					Assert(sl->subLinkId == list_length(ma_sublinks));
				}
			}
		}
	}
	next_ma_cell = list_head(ma_sublinks);
	cur_ma_sublink = NULL;
	remaining_ma_columns = 0;

	/* Add the comma separated list of 'attname = value' */
	sep = "";
	foreach(l, targetList)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(l);
		Node	   *expr;

		if (tle->resjunk)
			continue;			/* ignore junk entries */

		/* Emit separator (OK whether we're in multiassignment or not) */
		appendStringInfoString(buf, sep);
		sep = ", ";

		/*
		 * Check to see if we're starting a multiassignment group: if so,
		 * output a left paren.
		 */
		if (next_ma_cell != NULL && cur_ma_sublink == NULL)
		{
			/*
			 * We must dig down into the expr to see if it's a PARAM_MULTIEXPR
			 * Param.  That could be buried under FieldStores and
			 * SubscriptingRefs and CoerceToDomains (cf processIndirection()),
			 * and underneath those there could be an implicit type coercion.
			 * Because we would ignore implicit type coercions anyway, we
			 * don't need to be as careful as processIndirection() is about
			 * descending past implicit CoerceToDomains.
			 */
			expr = (Node *) tle->expr;
			while (expr)
			{
				if (IsA(expr, FieldStore))
				{
					FieldStore *fstore = (FieldStore *) expr;

					expr = (Node *) linitial(fstore->newvals);
				}
				else if (IsA(expr, SubscriptingRef))
				{
					SubscriptingRef *sbsref = (SubscriptingRef *) expr;

					if (sbsref->refassgnexpr == NULL)
						break;
					expr = (Node *) sbsref->refassgnexpr;
				}
				else if (IsA(expr, CoerceToDomain))
				{
					CoerceToDomain *cdomain = (CoerceToDomain *) expr;

					if (cdomain->coercionformat != COERCE_IMPLICIT_CAST)
						break;
					expr = (Node *) cdomain->arg;
				}
				else
					break;
			}
			expr = strip_implicit_coercions(expr);

			if (expr && IsA(expr, Param) &&
				((Param *) expr)->paramkind == PARAM_MULTIEXPR)
			{
				cur_ma_sublink = (SubLink *) lfirst(next_ma_cell);
				next_ma_cell = lnext(ma_sublinks, next_ma_cell);
				remaining_ma_columns = count_nonjunk_tlist_entries(
																   ((Query *) cur_ma_sublink->subselect)->targetList);
				Assert(((Param *) expr)->paramid ==
					   ((cur_ma_sublink->subLinkId << 16) | 1));
				appendStringInfoChar(buf, '(');
			}
		}

		/*
		 * Put out name of target column; look in the catalogs, not at
		 * tle->resname, since resname will fail to track RENAME.
		 */
		appendStringInfoString(buf,
							   quote_identifier(get_attname(rte->relid,
															tle->resno,
															false)));

		/*
		 * Print any indirection needed (subfields or subscripts), and strip
		 * off the top-level nodes representing the indirection assignments.
		 */
		expr = processIndirection((Node *) tle->expr, context);

		/*
		 * If we're in a multiassignment, skip printing anything more, unless
		 * this is the last column; in which case, what we print should be the
		 * sublink, not the Param.
		 */
		if (cur_ma_sublink != NULL)
		{
			if (--remaining_ma_columns > 0)
				continue;		/* not the last column of multiassignment */
			appendStringInfoChar(buf, ')');
			expr = (Node *) cur_ma_sublink;
			cur_ma_sublink = NULL;
		}

		appendStringInfoString(buf, " = ");

		get_rule_expr(expr, context, false);
	}
}

/* ----------
 * get_values_def			- Parse back a VALUES list
 * ----------
 */
static void
get_values_def(List *values_lists, deparse_context *context)
{
	StringInfo	buf = context->buf;
	bool		first_list = true;
	ListCell   *vtl;

	appendStringInfoString(buf, "VALUES ");

	foreach(vtl, values_lists)
	{
		List	   *sublist = (List *) lfirst(vtl);
		bool		first_col = true;
		ListCell   *lc;

		if (first_list)
			first_list = false;
		else
			appendStringInfoString(buf, ", ");

		appendStringInfoChar(buf, '(');
		foreach(lc, sublist)
		{
			Node	   *col = (Node *) lfirst(lc);

			if (first_col)
				first_col = false;
			else
				appendStringInfoChar(buf, ',');

			/*
			 * Print the value.  Whole-row Vars need special treatment.
			 */
			get_rule_expr_toplevel(col, context, false);
		}
		appendStringInfoChar(buf, ')');
	}
}

static void
get_coercion_expr(Node *arg, deparse_context *context,
				  Oid resulttype, int32 resulttypmod,
				  Node *parentNode)
{
	StringInfo	buf = context->buf;

	/*
	 * Since parse_coerce.c doesn't immediately collapse application of
	 * length-coercion functions to constants, what we'll typically see in
	 * such cases is a Const with typmod -1 and a length-coercion function
	 * right above it.  Avoid generating redundant output. However, beware of
	 * suppressing casts when the user actually wrote something like
	 * 'foo'::text::char(3).
	 *
	 * Note: it might seem that we are missing the possibility of needing to
	 * print a COLLATE clause for such a Const.  However, a Const could only
	 * have nondefault collation in a post-constant-folding tree, in which the
	 * length coercion would have been folded too.  See also the special
	 * handling of CollateExpr in coerce_to_target_type(): any collation
	 * marking will be above the coercion node, not below it.
	 */
	if (arg && IsA(arg, Const) &&
		((Const *) arg)->consttype == resulttype &&
		((Const *) arg)->consttypmod == -1)
	{
		/* Show the constant without normal ::typename decoration */
		get_const_expr((Const *) arg, context, -1);
	}
	else
	{
		if (!PRETTY_PAREN(context))
			appendStringInfoChar(buf, '(');
		get_rule_expr_paren(arg, context, false, parentNode);
		if (!PRETTY_PAREN(context))
			appendStringInfoChar(buf, ')');
	}
	appendStringInfo(buf, "::%s",
					 format_type_with_typemod(resulttype, resulttypmod));
}


static List *
CopyGetAttnums(TupleDesc tupDesc, Relation rel, List *attnamelist)
{
	/* *INDENT-OFF* */
	List	   *attnums = NIL;

	if (attnamelist == NIL)
	{
		/* Generate default column list */
		int			attr_count = tupDesc->natts;
		int			i;

		for (i = 0; i < attr_count; i++)
		{
			if (TupleDescAttr(tupDesc, i)->attisdropped)
				continue;
			if (TupleDescAttr(tupDesc, i)->attgenerated)
				continue;
			attnums = lappend_int(attnums, i + 1);
		}
	}
	else
	{
		/* Validate the user-supplied list and extract attnums */
		ListCell   *l;

		foreach(l, attnamelist)
		{
			char	   *name = strVal(lfirst(l));
			int			attnum;
			int			i;

			/* Lookup column name */
			attnum = InvalidAttrNumber;
			for (i = 0; i < tupDesc->natts; i++)
			{
				Form_pg_attribute att = TupleDescAttr(tupDesc, i);

				if (att->attisdropped)
					continue;
				if (namestrcmp(&(att->attname), name) == 0)
				{
					if (att->attgenerated)
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_COLUMN_REFERENCE),
								 errmsg("column \"%s\" is a generated column",
										name),
								 errdetail("Generated columns cannot be used in COPY.")));
					attnum = att->attnum;
					break;
				}
			}
			if (attnum == InvalidAttrNumber)
			{
				if (rel != NULL)
					ereport(ERROR,
					        (errcode(ERRCODE_UNDEFINED_COLUMN),
							        errmsg("column \"%s\" of relation \"%s\" does not exist",
							               name, RelationGetRelationName(rel))));
				else
					ereport(ERROR,
					        (errcode(ERRCODE_UNDEFINED_COLUMN),
							        errmsg("column \"%s\" does not exist",
							               name)));
			}
			/* Check for duplicates */
			if (list_member_int(attnums, attnum))
				ereport(ERROR,
				        (errcode(ERRCODE_DUPLICATE_COLUMN),
						        errmsg("column \"%s\" specified more than once",
						               name)));
			attnums = lappend_int(attnums, attnum);
		}
	}

	return attnums;
	/* *INDENT-ON* */
}

static char *
generate_function_name(Oid funcid, int nargs, List *argnames, Oid *argtypes,
					   bool has_variadic, bool *use_variadic_p,
					   ParseExprKind special_exprkind)
{
	char	   *result;
	HeapTuple	proctup;
	Form_pg_proc procform;
	char	   *proname;
	bool		use_variadic;
	char	   *nspname;
	FuncDetailCode p_result;
	Oid			p_funcid;
	Oid			p_rettype;
	bool		p_retset;
	int			p_nvargs;
	Oid			p_vatype;
	Oid		   *p_true_typeids;
	bool		force_qualify = false;

	proctup = SearchSysCache1(PROCOID, ObjectIdGetDatum(funcid));
	if (!HeapTupleIsValid(proctup))
		elog(ERROR, "cache lookup failed for function %u", funcid);
	procform = (Form_pg_proc) GETSTRUCT(proctup);
	proname = NameStr(procform->proname);

	/*
	 * Due to parser hacks to avoid needing to reserve CUBE, we need to force
	 * qualification in some special cases.
	 */
	if (special_exprkind == EXPR_KIND_GROUP_BY)
	{
		if (strcmp(proname, "cube") == 0 || strcmp(proname, "rollup") == 0)
			force_qualify = true;
	}

	/*
	 * Determine whether VARIADIC should be printed.  We must do this first
	 * since it affects the lookup rules in func_get_detail().
	 *
	 * Currently, we always print VARIADIC if the function has a merged
	 * variadic-array argument.  Note that this is always the case for
	 * functions taking a VARIADIC argument type other than VARIADIC ANY.
	 *
	 * In principle, if VARIADIC wasn't originally specified and the array
	 * actual argument is deconstructable, we could print the array elements
	 * separately and not print VARIADIC, thus more nearly reproducing the
	 * original input.  For the moment that seems like too much complication
	 * for the benefit, and anyway we do not know whether VARIADIC was
	 * originally specified if it's a non-ANY type.
	 */
	if (use_variadic_p)
	{
		/* Parser should not have set funcvariadic unless fn is variadic */
		Assert(!has_variadic || OidIsValid(procform->provariadic));
		use_variadic = has_variadic;
		*use_variadic_p = use_variadic;
	}
	else
	{
		Assert(!has_variadic);
		use_variadic = false;
	}

	/*
	 * The idea here is to schema-qualify only if the parser would fail to
	 * resolve the correct function given the unqualified func name with the
	 * specified argtypes and VARIADIC flag.  But if we already decided to
	 * force qualification, then we can skip the lookup and pretend we didn't
	 * find it.
	 */
	if (!force_qualify)
		p_result = func_get_detail(list_make1(makeString(proname)),
								   NIL, argnames, nargs, argtypes,
								   !use_variadic, true, false,
								   &p_funcid, &p_rettype,
								   &p_retset, &p_nvargs, &p_vatype,
								   &p_true_typeids, NULL);
	else
	{
		p_result = FUNCDETAIL_NOTFOUND;
		p_funcid = InvalidOid;
	}

	if ((p_result == FUNCDETAIL_NORMAL ||
		 p_result == FUNCDETAIL_AGGREGATE ||
		 p_result == FUNCDETAIL_WINDOWFUNC) &&
		p_funcid == funcid)
		nspname = NULL;
	else
		nspname = get_namespace_name_or_temp(procform->pronamespace);

	result = quote_qualified_identifier(nspname, proname);

	ReleaseSysCache(proctup);

	return result;
}


List *
getOwnedSequences_internal(Oid relid, AttrNumber attnum, char deptype)
{
	List	   *result = NIL;
	Relation	depRel;
	ScanKeyData key[3];
	SysScanDesc scan;
	HeapTuple	tup;

	depRel = table_open(DependRelationId, AccessShareLock);

	ScanKeyInit(&key[0],
				Anum_pg_depend_refclassid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationRelationId));
	ScanKeyInit(&key[1],
				Anum_pg_depend_refobjid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(relid));
	if (attnum)
		ScanKeyInit(&key[2],
					Anum_pg_depend_refobjsubid,
					BTEqualStrategyNumber, F_INT4EQ,
					Int32GetDatum(attnum));

	scan = systable_beginscan(depRel, DependReferenceIndexId, true,
							  NULL, attnum ? 3 : 2, key);

	while (HeapTupleIsValid(tup = systable_getnext(scan)))
	{
		Form_pg_depend deprec = (Form_pg_depend) GETSTRUCT(tup);

		/*
		 * We assume any auto or internal dependency of a sequence on a column
		 * must be what we are looking for.  (We need the relkind test because
		 * indexes can also have auto dependencies on columns.)
		 */
		if (deprec->classid == RelationRelationId &&
			deprec->objsubid == 0 &&
			deprec->refobjsubid != 0 &&
			(deprec->deptype == DEPENDENCY_AUTO || deprec->deptype == DEPENDENCY_INTERNAL) &&
			get_rel_relkind(deprec->objid) == RELKIND_SEQUENCE)
		{
			if (!deptype || deprec->deptype == deptype)
				result = lappend_oid(result, deprec->objid);
		}
	}

	systable_endscan(scan);

	table_close(depRel, AccessShareLock);

	return result;
}




char *
generate_relation_name(Oid relid, List *namespaces)
{
	HeapTuple	tp;
	Form_pg_class reltup;
	bool		need_qual;
	ListCell   *nslist;
	char	   *relname;
	char	   *nspname;
	char	   *result;

	tp = SearchSysCache1(RELOID, ObjectIdGetDatum(relid));
	if (!HeapTupleIsValid(tp))
		elog(ERROR, "cache lookup failed for relation %u", relid);
	reltup = (Form_pg_class) GETSTRUCT(tp);
	relname = NameStr(reltup->relname);

	/* Check for conflicting CTE name */
	need_qual = false;
	foreach(nslist, namespaces)
	{
		deparse_namespace *dpns = (deparse_namespace *) lfirst(nslist);
		ListCell   *ctlist;

		foreach(ctlist, dpns->ctes)
		{
			CommonTableExpr *cte = (CommonTableExpr *) lfirst(ctlist);

			if (strcmp(cte->ctename, relname) == 0)
			{
				need_qual = true;
				break;
			}
		}
		if (need_qual)
			break;
	}

	/* Otherwise, qualify the name if not visible in search path */
	if (!need_qual)
		need_qual = !RelationIsVisible(relid);

	if (need_qual)
		nspname = get_namespace_name_or_temp(reltup->relnamespace);
	else
		nspname = NULL;

	result = quote_qualified_identifier(nspname, relname);

	ReleaseSysCache(tp);

	return result;
}

/*
 * get_simple_binary_op_name
 *
 * helper function for isSimpleNode
 * will return single char binary operator name, or NULL if it's not
 */
static const char *
get_simple_binary_op_name(OpExpr *expr)
{
	List	   *args = expr->args;

	if (list_length(args) == 2)
	{
		/* binary operator */
		Node	   *arg1 = (Node *) linitial(args);
		Node	   *arg2 = (Node *) lsecond(args);
		const char *op;

		op = generate_operator_name(expr->opno, exprType(arg1), exprType(arg2));
		if (strlen(op) == 1)
			return op;
	}
	return NULL;
}

/*
 * push_child_plan: temporarily transfer deparsing attention to a child plan
 *
 * When expanding an OUTER_VAR or INNER_VAR reference, we must adjust the
 * deparse context in case the referenced expression itself uses
 * OUTER_VAR/INNER_VAR.  We modify the top stack entry in-place to avoid
 * affecting levelsup issues (although in a Plan tree there really shouldn't
 * be any).
 *
 * Caller must provide a local deparse_namespace variable to save the
 * previous state for pop_child_plan.
 */
static void
push_child_plan(deparse_namespace *dpns, Plan *plan,
				deparse_namespace *save_dpns)
{
	/* Save state for restoration later */
	*save_dpns = *dpns;

	/* Link current plan node into ancestors list */
	dpns->ancestors = lcons(dpns->plan, dpns->ancestors);

	/* Set attention on selected child */
	set_deparse_plan(dpns, plan);
}

/*
 * pop_child_plan: undo the effects of push_child_plan
 */
static void
pop_child_plan(deparse_namespace *dpns, deparse_namespace *save_dpns)
{
	List	   *ancestors;

	/* Get rid of ancestors list cell added by push_child_plan */
	ancestors = list_delete_first(dpns->ancestors);

	/* Restore fields changed by push_child_plan */
	*dpns = *save_dpns;

	/* Make sure dpns->ancestors is right (may be unnecessary) */
	dpns->ancestors = ancestors;
}

/*
 * push_ancestor_plan: temporarily transfer deparsing attention to an
 * ancestor plan
 *
 * When expanding a Param reference, we must adjust the deparse context
 * to match the plan node that contains the expression being printed;
 * otherwise we'd fail if that expression itself contains a Param or
 * OUTER_VAR/INNER_VAR/INDEX_VAR variable.
 *
 * The target ancestor is conveniently identified by the ListCell holding it
 * in dpns->ancestors.
 *
 * Caller must provide a local deparse_namespace variable to save the
 * previous state for pop_ancestor_plan.
 */
static void
push_ancestor_plan(deparse_namespace *dpns, ListCell *ancestor_cell,
				   deparse_namespace *save_dpns)
{
	Plan	   *plan = (Plan *) lfirst(ancestor_cell);

	/* Save state for restoration later */
	*save_dpns = *dpns;

	/* Build a new ancestor list with just this node's ancestors */
	dpns->ancestors =
		list_copy_tail(dpns->ancestors,
					   list_cell_number(dpns->ancestors, ancestor_cell) + 1);

	/* Set attention on selected ancestor */
	set_deparse_plan(dpns, plan);
}

/*
 * pop_ancestor_plan: undo the effects of push_ancestor_plan
 */
static void
pop_ancestor_plan(deparse_namespace *dpns, deparse_namespace *save_dpns)
{
	/* Free the ancestor list made in push_ancestor_plan */
	list_free(dpns->ancestors);

	/* Restore fields changed by push_ancestor_plan */
	*dpns = *save_dpns;
}

/*
 * get_one_range_partition_bound_string
 *		A C string representation of one range partition bound
 */
char *
get_range_partbound_string(List *bound_datums)
{
	deparse_context context;
	StringInfo	buf = makeStringInfo();
	ListCell   *cell;
	char	   *sep;

	memset(&context, 0, sizeof(deparse_context));
	context.buf = buf;

	appendStringInfoChar(buf, '(');
	sep = "";
	foreach(cell, bound_datums)
	{
		PartitionRangeDatum *datum =
		lfirst_node(PartitionRangeDatum, cell);

		appendStringInfoString(buf, sep);
		if (datum->kind == PARTITION_RANGE_DATUM_MINVALUE)
			appendStringInfoString(buf, "MINVALUE");
		else if (datum->kind == PARTITION_RANGE_DATUM_MAXVALUE)
			appendStringInfoString(buf, "MAXVALUE");
		else
		{
			Const	   *val = castNode(Const, datum->value);

			get_const_expr(val, &context, -1);
		}
		sep = ", ";
	}
	appendStringInfoChar(buf, ')');

	return buf->data;
}


char *
ChooseIndexName(const char *tabname, Oid namespaceId,
				List *colnames, List *exclusionOpNames,
				bool primary, bool isconstraint)
{
	char *indexname;

	if (primary)
	{
		/* the primary key's name does not depend on the specific column(s) */
		indexname = ChooseRelationName(tabname,
									   NULL,
									   "pkey",
									   namespaceId,
									   true);
	}
	else if (exclusionOpNames != NIL)
	{
		indexname = ChooseRelationName(tabname,
									   ChooseIndexNameAddition(colnames),
									   "excl",
									   namespaceId,
									   true);
	}
	else if (isconstraint)
	{
		indexname = ChooseRelationName(tabname,
									   ChooseIndexNameAddition(colnames),
									   "key",
									   namespaceId,
									   true);
	}
	else
	{
		indexname = ChooseRelationName(tabname,
									   ChooseIndexNameAddition(colnames),
									   "idx",
									   namespaceId,
									   false);
	}

	return indexname;
}

/*
 * get_rule_expr_funccall		- Parse back a function-call expression
 *
 * Same as get_rule_expr(), except that we guarantee that the output will
 * look like a function call, or like one of the things the grammar treats as
 * equivalent to a function call (see the func_expr_windowless production).
 * This is needed in places where the grammar uses func_expr_windowless and
 * you can't substitute a parenthesized a_expr.  If what we have isn't going
 * to look like a function call, wrap it in a dummy CAST() expression, which
 * will satisfy the grammar --- and, indeed, is likely what the user wrote to
 * produce such a thing.
 */
static void
get_rule_expr_funccall(Node *node, deparse_context *context,
					   bool showimplicit)
{
	if (looks_like_function(node))
		get_rule_expr(node, context, showimplicit);
	else
	{
		StringInfo	buf = context->buf;

		appendStringInfoString(buf, "CAST(");
		/* no point in showing any top-level implicit cast */
		get_rule_expr(node, context, false);
		appendStringInfo(buf, " AS %s)",
						 format_type_with_typemod(exprType(node),
												  exprTypmod(node)));
	}
}

/*
 * contain_dml: is any subquery not a plain SELECT?
 *
 * We reject SELECT FOR UPDATE/SHARE as well as INSERT etc.
 */
static bool
contain_dml(Node *node)
{
	return contain_dml_walker(node, NULL);
}


static bool
contain_dml_walker(Node *node, void *context)
{
	if (node == NULL)
		return false;
	if (IsA(node, Query))
	{
		Query *query = (Query *) node;

		if (query->commandType != CMD_SELECT ||
			query->rowMarks != NIL)
			return true;

		return query_tree_walker(query, contain_dml_walker, context, 0);
	}
	return expression_tree_walker(node, contain_dml_walker, context);
}
