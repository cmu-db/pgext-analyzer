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

/* ----------
 * get_select_query_def			- Parse back a SELECT parsetree
 * ----------
 */
static void
get_select_query_def(Query *query, deparse_context *context,
					 TupleDesc resultDesc, bool colNamesVisible)
{
	StringInfo	buf = context->buf;
	List	   *save_windowclause;
	List	   *save_windowtlist;
	bool		force_colno;
	ListCell   *l;

	/* Insert the WITH clause if given */
	get_with_clause(query, context);

	/* Set up context for possible window functions */
	save_windowclause = context->windowClause;
	context->windowClause = query->windowClause;
	save_windowtlist = context->windowTList;
	context->windowTList = query->targetList;

	/*
	 * If the Query node has a setOperations tree, then it's the top level of
	 * a UNION/INTERSECT/EXCEPT query; only the WITH, ORDER BY and LIMIT
	 * fields are interesting in the top query itself.
	 */
	if (query->setOperations)
	{
		get_setop_query(query->setOperations, query, context, resultDesc,
						colNamesVisible);
		/* ORDER BY clauses must be simple in this case */
		force_colno = true;
	}
	else
	{
		get_basic_select_query(query, context, resultDesc, colNamesVisible);
		force_colno = false;
	}

	/* Add the ORDER BY clause if given */
	if (query->sortClause != NIL)
	{
		appendContextKeyword(context, " ORDER BY ",
							 -PRETTYINDENT_STD, PRETTYINDENT_STD, 1);
		get_rule_orderby(query->sortClause, query->targetList,
						 force_colno, context);
	}

	/*
	 * Add the LIMIT/OFFSET clauses if given. If non-default options, use the
	 * standard spelling of LIMIT.
	 */
	if (query->limitOffset != NULL)
	{
		appendContextKeyword(context, " OFFSET ",
							 -PRETTYINDENT_STD, PRETTYINDENT_STD, 0);
		get_rule_expr(query->limitOffset, context, false);
	}
	if (query->limitCount != NULL)
	{
		if (query->limitOption == LIMIT_OPTION_WITH_TIES)
		{
			appendContextKeyword(context, " FETCH FIRST ",
								 -PRETTYINDENT_STD, PRETTYINDENT_STD, 0);
			get_rule_expr(query->limitCount, context, false);
			appendStringInfoString(buf, " ROWS WITH TIES");
		}
		else
		{
			appendContextKeyword(context, " LIMIT ",
								 -PRETTYINDENT_STD, PRETTYINDENT_STD, 0);
			if (IsA(query->limitCount, Const) &&
				((Const *) query->limitCount)->constisnull)
				appendStringInfoString(buf, "ALL");
			else
				get_rule_expr(query->limitCount, context, false);
		}
	}

	/* Add FOR [KEY] UPDATE/SHARE clauses if present */
	if (query->hasForUpdate)
	{
		foreach(l, query->rowMarks)
		{
			RowMarkClause *rc = (RowMarkClause *) lfirst(l);

			/* don't print implicit clauses */
			if (rc->pushedDown)
				continue;

			switch (rc->strength)
			{
				case LCS_NONE:
					/* we intentionally throw an error for LCS_NONE */
					elog(ERROR, "unrecognized LockClauseStrength %d",
						 (int) rc->strength);
					break;
				case LCS_FORKEYSHARE:
					appendContextKeyword(context, " FOR KEY SHARE",
										 -PRETTYINDENT_STD, PRETTYINDENT_STD, 0);
					break;
				case LCS_FORSHARE:
					appendContextKeyword(context, " FOR SHARE",
										 -PRETTYINDENT_STD, PRETTYINDENT_STD, 0);
					break;
				case LCS_FORNOKEYUPDATE:
					appendContextKeyword(context, " FOR NO KEY UPDATE",
										 -PRETTYINDENT_STD, PRETTYINDENT_STD, 0);
					break;
				case LCS_FORUPDATE:
					appendContextKeyword(context, " FOR UPDATE",
										 -PRETTYINDENT_STD, PRETTYINDENT_STD, 0);
					break;
			}

			appendStringInfo(buf, " OF %s",
							 quote_identifier(get_rtable_name(rc->rti,
															  context)));
			if (rc->waitPolicy == LockWaitError)
				appendStringInfoString(buf, " NOWAIT");
			else if (rc->waitPolicy == LockWaitSkip)
				appendStringInfoString(buf, " SKIP LOCKED");
		}
	}

	context->windowClause = save_windowclause;
	context->windowTList = save_windowtlist;
}

/*
 * Detect whether query looks like SELECT ... FROM VALUES(),
 * with no need to rename the output columns of the VALUES RTE.
 * If so, return the VALUES RTE.  Otherwise return NULL.
 */
static RangeTblEntry *
get_simple_values_rte(Query *query, TupleDesc resultDesc)
{
	RangeTblEntry *result = NULL;
	ListCell   *lc;

	/*
	 * We want to detect a match even if the Query also contains OLD or NEW
	 * rule RTEs.  So the idea is to scan the rtable and see if there is only
	 * one inFromCl RTE that is a VALUES RTE.
	 */
	foreach(lc, query->rtable)
	{
		RangeTblEntry *rte = (RangeTblEntry *) lfirst(lc);

		if (rte->rtekind == RTE_VALUES && rte->inFromCl)
		{
			if (result)
				return NULL;	/* multiple VALUES (probably not possible) */
			result = rte;
		}
		else if (rte->rtekind == RTE_RELATION && !rte->inFromCl)
			continue;			/* ignore rule entries */
		else
			return NULL;		/* something else -> not simple VALUES */
	}

	/*
	 * We don't need to check the targetlist in any great detail, because
	 * parser/analyze.c will never generate a "bare" VALUES RTE --- they only
	 * appear inside auto-generated sub-queries with very restricted
	 * structure.  However, DefineView might have modified the tlist by
	 * injecting new column aliases, or we might have some other column
	 * aliases forced by a resultDesc.  We can only simplify if the RTE's
	 * column names match the names that get_target_list() would select.
	 */
	if (result)
	{
		ListCell   *lcn;
		int			colno;

		if (list_length(query->targetList) != list_length(result->eref->colnames))
			return NULL;		/* this probably cannot happen */
		colno = 0;
		forboth(lc, query->targetList, lcn, result->eref->colnames)
		{
			TargetEntry *tle = (TargetEntry *) lfirst(lc);
			char	   *cname = strVal(lfirst(lcn));
			char	   *colname;

			if (tle->resjunk)
				return NULL;	/* this probably cannot happen */

			/* compute name that get_target_list would use for column */
			colno++;
			if (resultDesc && colno <= resultDesc->natts)
				colname = NameStr(TupleDescAttr(resultDesc, colno - 1)->attname);
			else
				colname = tle->resname;

			/* does it match the VALUES RTE? */
			if (colname == NULL || strcmp(colname, cname) != 0)
				return NULL;	/* column name has been changed */
		}
	}

	return result;
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
 * get_target_list			- Parse back a SELECT target list
 *
 * This is also used for RETURNING lists in INSERT/UPDATE/DELETE.
 *
 * resultDesc and colNamesVisible are as for get_query_def()
 * ----------
 */
static void
get_target_list(List *targetList, deparse_context *context,
				TupleDesc resultDesc, bool colNamesVisible)
{
	StringInfo	buf = context->buf;
	StringInfoData targetbuf;
	bool		last_was_multiline = false;
	char	   *sep;
	int			colno;
	ListCell   *l;

	/* we use targetbuf to hold each TLE's text temporarily */
	initStringInfo(&targetbuf);

	sep = " ";
	colno = 0;
	foreach(l, targetList)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(l);
		char	   *colname;
		char	   *attname;

		if (tle->resjunk)
			continue;			/* ignore junk entries */

		appendStringInfoString(buf, sep);
		sep = ", ";
		colno++;

		/*
		 * Put the new field text into targetbuf so we can decide after we've
		 * got it whether or not it needs to go on a new line.
		 */
		resetStringInfo(&targetbuf);
		context->buf = &targetbuf;

		/*
		 * We special-case Var nodes rather than using get_rule_expr. This is
		 * needed because get_rule_expr will display a whole-row Var as
		 * "foo.*", which is the preferred notation in most contexts, but at
		 * the top level of a SELECT list it's not right (the parser will
		 * expand that notation into multiple columns, yielding behavior
		 * different from a whole-row Var).  We need to call get_variable
		 * directly so that we can tell it to do the right thing, and so that
		 * we can get the attribute name which is the default AS label.
		 */
		if (tle->expr && (IsA(tle->expr, Var)))
		{
			attname = get_variable((Var *) tle->expr, 0, true, context);
		}
		else
		{
			get_rule_expr((Node *) tle->expr, context, true);

			/*
			 * When colNamesVisible is true, we should always show the
			 * assigned column name explicitly.  Otherwise, show it only if
			 * it's not FigureColname's fallback.
			 */
			attname = colNamesVisible ? NULL : "?column?";
		}

		/*
		 * Figure out what the result column should be called.  In the context
		 * of a view, use the view's tuple descriptor (so as to pick up the
		 * effects of any column RENAME that's been done on the view).
		 * Otherwise, just use what we can find in the TLE.
		 */
		if (resultDesc && colno <= resultDesc->natts)
			colname = NameStr(TupleDescAttr(resultDesc, colno - 1)->attname);
		else
			colname = tle->resname;

		/* Show AS unless the column's name is correct as-is */
		if (colname)			/* resname could be NULL */
		{
			if (attname == NULL || strcmp(attname, colname) != 0)
				appendStringInfo(&targetbuf, " AS %s", quote_identifier(colname));
		}

		/* Restore context's output buffer */
		context->buf = buf;

		/* Consider line-wrapping if enabled */
		if (PRETTY_INDENT(context) && context->wrapColumn >= 0)
		{
			int			leading_nl_pos;

			/* Does the new field start with a new line? */
			if (targetbuf.len > 0 && targetbuf.data[0] == '\n')
				leading_nl_pos = 0;
			else
				leading_nl_pos = -1;

			/* If so, we shouldn't add anything */
			if (leading_nl_pos >= 0)
			{
				/* instead, remove any trailing spaces currently in buf */
				removeStringInfoSpaces(buf);
			}
			else
			{
				char	   *trailing_nl;

				/* Locate the start of the current line in the output buffer */
				trailing_nl = strrchr(buf->data, '\n');
				if (trailing_nl == NULL)
					trailing_nl = buf->data;
				else
					trailing_nl++;

				/*
				 * Add a newline, plus some indentation, if the new field is
				 * not the first and either the new field would cause an
				 * overflow or the last field used more than one line.
				 */
				if (colno > 1 &&
					((strlen(trailing_nl) + targetbuf.len > context->wrapColumn) ||
					 last_was_multiline))
					appendContextKeyword(context, "", -PRETTYINDENT_STD,
										 PRETTYINDENT_STD, PRETTYINDENT_VAR);
			}

			/* Remember this field's multiline status for next iteration */
			last_was_multiline =
				(strchr(targetbuf.data + leading_nl_pos + 1, '\n') != NULL);
		}

		/* Add the new field */
		appendBinaryStringInfo(buf, targetbuf.data, targetbuf.len);
	}

	/* clean up */
	pfree(targetbuf.data);
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
 * set_relation_column_names: select column aliases for a non-join RTE
 *
 * Column alias info is saved in *colinfo, which is assumed to be pre-zeroed.
 * If any colnames entries are already filled in, those override local
 * choices.
 */
static void
set_relation_column_names(deparse_namespace *dpns, RangeTblEntry *rte,
						  deparse_columns *colinfo)
{
	int			ncolumns;
	char	  **real_colnames;
	bool		changed_any;
	int			noldcolumns;
	int			i;
	int			j;

	/*
	 * Construct an array of the current "real" column names of the RTE.
	 * real_colnames[] will be indexed by physical column number, with NULL
	 * entries for dropped columns.
	 */
	if (rte->rtekind == RTE_RELATION)
	{
		/* Relation --- look to the system catalogs for up-to-date info */
		Relation	rel;
		TupleDesc	tupdesc;

		rel = relation_open(rte->relid, AccessShareLock);
		tupdesc = RelationGetDescr(rel);

		ncolumns = tupdesc->natts;
		real_colnames = (char **) palloc(ncolumns * sizeof(char *));

		for (i = 0; i < ncolumns; i++)
		{
			Form_pg_attribute attr = TupleDescAttr(tupdesc, i);

			if (attr->attisdropped)
				real_colnames[i] = NULL;
			else
				real_colnames[i] = pstrdup(NameStr(attr->attname));
		}
		relation_close(rel, AccessShareLock);
	}
	else
	{
		/* Otherwise get the column names from eref or expandRTE() */
		List	   *colnames;
		ListCell   *lc;

		/*
		 * Functions returning composites have the annoying property that some
		 * of the composite type's columns might have been dropped since the
		 * query was parsed.  If possible, use expandRTE() to handle that
		 * case, since it has the tedious logic needed to find out about
		 * dropped columns.  However, if we're explaining a plan, then we
		 * don't have rte->functions because the planner thinks that won't be
		 * needed later, and that breaks expandRTE().  So in that case we have
		 * to rely on rte->eref, which may lead us to report a dropped
		 * column's old name; that seems close enough for EXPLAIN's purposes.
		 *
		 * For non-RELATION, non-FUNCTION RTEs, we can just look at rte->eref,
		 * which should be sufficiently up-to-date: no other RTE types can
		 * have columns get dropped from under them after parsing.
		 */
		if (rte->rtekind == RTE_FUNCTION && rte->functions != NIL)
		{
			/* Since we're not creating Vars, rtindex etc. don't matter */
			expandRTE(rte, 1, 0, -1, true /* include dropped */ ,
					  &colnames, NULL);
		}
		else
			colnames = rte->eref->colnames;

		ncolumns = list_length(colnames);
		real_colnames = (char **) palloc(ncolumns * sizeof(char *));

		i = 0;
		foreach(lc, colnames)
		{
			/*
			 * If the column name we find here is an empty string, then it's a
			 * dropped column, so change to NULL.
			 */
			char	   *cname = strVal(lfirst(lc));

			if (cname[0] == '\0')
				cname = NULL;
			real_colnames[i] = cname;
			i++;
		}
	}

	/*
	 * Ensure colinfo->colnames has a slot for each column.  (It could be long
	 * enough already, if we pushed down a name for the last column.)  Note:
	 * it's possible that there are now more columns than there were when the
	 * query was parsed, ie colnames could be longer than rte->eref->colnames.
	 * We must assign unique aliases to the new columns too, else there could
	 * be unresolved conflicts when the view/rule is reloaded.
	 */
	expand_colnames_array_to(colinfo, ncolumns);
	Assert(colinfo->num_cols == ncolumns);

	/*
	 * Make sufficiently large new_colnames and is_new_col arrays, too.
	 *
	 * Note: because we leave colinfo->num_new_cols zero until after the loop,
	 * colname_is_unique will not consult that array, which is fine because it
	 * would only be duplicate effort.
	 */
	colinfo->new_colnames = (char **) palloc(ncolumns * sizeof(char *));
	colinfo->is_new_col = (bool *) palloc(ncolumns * sizeof(bool));

	/*
	 * Scan the columns, select a unique alias for each one, and store it in
	 * colinfo->colnames and colinfo->new_colnames.  The former array has NULL
	 * entries for dropped columns, the latter omits them.  Also mark
	 * new_colnames entries as to whether they are new since parse time; this
	 * is the case for entries beyond the length of rte->eref->colnames.
	 */
	noldcolumns = list_length(rte->eref->colnames);
	changed_any = false;
	j = 0;
	for (i = 0; i < ncolumns; i++)
	{
		char	   *real_colname = real_colnames[i];
		char	   *colname = colinfo->colnames[i];

		/* Skip dropped columns */
		if (real_colname == NULL)
		{
			Assert(colname == NULL);	/* colnames[i] is already NULL */
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

		/* Put names of non-dropped columns in new_colnames[] too */
		colinfo->new_colnames[j] = colname;
		/* And mark them as new or not */
		colinfo->is_new_col[j] = (i >= noldcolumns);
		j++;

		/* Remember if any assigned aliases differ from "real" name */
		if (!changed_any && strcmp(colname, real_colname) != 0)
			changed_any = true;
	}

	/*
	 * Set correct length for new_colnames[] array.  (Note: if columns have
	 * been added, colinfo->num_cols includes them, which is not really quite
	 * right but is harmless, since any new columns must be at the end where
	 * they won't affect varattnos of pre-existing columns.)
	 */
	colinfo->num_new_cols = j;

	/*
	 * For a relation RTE, we need only print the alias column names if any
	 * are different from the underlying "real" names.  For a function RTE,
	 * always emit a complete column alias list; this is to protect against
	 * possible instability of the default column names (eg, from altering
	 * parameter names).  For tablefunc RTEs, we never print aliases, because
	 * the column names are part of the clause itself.  For other RTE types,
	 * print if we changed anything OR if there were user-written column
	 * aliases (since the latter would be part of the underlying "reality").
	 */
	if (rte->rtekind == RTE_RELATION)
		colinfo->printaliases = changed_any;
	else if (rte->rtekind == RTE_FUNCTION)
		colinfo->printaliases = true;
	else if (rte->rtekind == RTE_TABLEFUNC)
		colinfo->printaliases = false;
	else if (rte->alias && rte->alias->colnames != NIL)
		colinfo->printaliases = true;
	else
		colinfo->printaliases = changed_any;
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
 * set_relation_column_names: select column aliases for a non-join RTE
 *
 * Column alias info is saved in *colinfo, which is assumed to be pre-zeroed.
 * If any colnames entries are already filled in, those override local
 * choices.
 */
static void
set_relation_column_names(deparse_namespace *dpns, RangeTblEntry *rte,
						  deparse_columns *colinfo)
{
	int			ncolumns;
	char	  **real_colnames;
	bool		changed_any;
	int			noldcolumns;
	int			i;
	int			j;

	/*
	 * Construct an array of the current "real" column names of the RTE.
	 * real_colnames[] will be indexed by physical column number, with NULL
	 * entries for dropped columns.
	 */
	if (rte->rtekind == RTE_RELATION)
	{
		/* Relation --- look to the system catalogs for up-to-date info */
		Relation	rel;
		TupleDesc	tupdesc;

		rel = relation_open(rte->relid, AccessShareLock);
		tupdesc = RelationGetDescr(rel);

		ncolumns = tupdesc->natts;
		real_colnames = (char **) palloc(ncolumns * sizeof(char *));

		for (i = 0; i < ncolumns; i++)
		{
			Form_pg_attribute attr = TupleDescAttr(tupdesc, i);

			if (attr->attisdropped)
				real_colnames[i] = NULL;
			else
				real_colnames[i] = pstrdup(NameStr(attr->attname));
		}
		relation_close(rel, AccessShareLock);
	}
	else
	{
		/* Otherwise get the column names from eref or expandRTE() */
		List	   *colnames;
		ListCell   *lc;

		/*
		 * Functions returning composites have the annoying property that some
		 * of the composite type's columns might have been dropped since the
		 * query was parsed.  If possible, use expandRTE() to handle that
		 * case, since it has the tedious logic needed to find out about
		 * dropped columns.  However, if we're explaining a plan, then we
		 * don't have rte->functions because the planner thinks that won't be
		 * needed later, and that breaks expandRTE().  So in that case we have
		 * to rely on rte->eref, which may lead us to report a dropped
		 * column's old name; that seems close enough for EXPLAIN's purposes.
		 *
		 * For non-RELATION, non-FUNCTION RTEs, we can just look at rte->eref,
		 * which should be sufficiently up-to-date: no other RTE types can
		 * have columns get dropped from under them after parsing.
		 */
		if (rte->rtekind == RTE_FUNCTION && rte->functions != NIL)
		{
			/* Since we're not creating Vars, rtindex etc. don't matter */
			expandRTE(rte, 1, 0, -1, true /* include dropped */ ,
					  &colnames, NULL);
		}
		else
			colnames = rte->eref->colnames;

		ncolumns = list_length(colnames);
		real_colnames = (char **) palloc(ncolumns * sizeof(char *));

		i = 0;
		foreach(lc, colnames)
		{
			/*
			 * If the column name we find here is an empty string, then it's a
			 * dropped column, so change to NULL.
			 */
			char	   *cname = strVal(lfirst(lc));

			if (cname[0] == '\0')
				cname = NULL;
			real_colnames[i] = cname;
			i++;
		}
	}

	/*
	 * Ensure colinfo->colnames has a slot for each column.  (It could be long
	 * enough already, if we pushed down a name for the last column.)  Note:
	 * it's possible that there are now more columns than there were when the
	 * query was parsed, ie colnames could be longer than rte->eref->colnames.
	 * We must assign unique aliases to the new columns too, else there could
	 * be unresolved conflicts when the view/rule is reloaded.
	 */
	expand_colnames_array_to(colinfo, ncolumns);
	Assert(colinfo->num_cols == ncolumns);

	/*
	 * Make sufficiently large new_colnames and is_new_col arrays, too.
	 *
	 * Note: because we leave colinfo->num_new_cols zero until after the loop,
	 * colname_is_unique will not consult that array, which is fine because it
	 * would only be duplicate effort.
	 */
	colinfo->new_colnames = (char **) palloc(ncolumns * sizeof(char *));
	colinfo->is_new_col = (bool *) palloc(ncolumns * sizeof(bool));

	/*
	 * Scan the columns, select a unique alias for each one, and store it in
	 * colinfo->colnames and colinfo->new_colnames.  The former array has NULL
	 * entries for dropped columns, the latter omits them.  Also mark
	 * new_colnames entries as to whether they are new since parse time; this
	 * is the case for entries beyond the length of rte->eref->colnames.
	 */
	noldcolumns = list_length(rte->eref->colnames);
	changed_any = false;
	j = 0;
	for (i = 0; i < ncolumns; i++)
	{
		char	   *real_colname = real_colnames[i];
		char	   *colname = colinfo->colnames[i];

		/* Skip dropped columns */
		if (real_colname == NULL)
		{
			Assert(colname == NULL);	/* colnames[i] is already NULL */
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

		/* Put names of non-dropped columns in new_colnames[] too */
		colinfo->new_colnames[j] = colname;
		/* And mark them as new or not */
		colinfo->is_new_col[j] = (i >= noldcolumns);
		j++;

		/* Remember if any assigned aliases differ from "real" name */
		if (!changed_any && strcmp(colname, real_colname) != 0)
			changed_any = true;
	}

	/*
	 * Set correct length for new_colnames[] array.  (Note: if columns have
	 * been added, colinfo->num_cols includes them, which is not really quite
	 * right but is harmless, since any new columns must be at the end where
	 * they won't affect varattnos of pre-existing columns.)
	 */
	colinfo->num_new_cols = j;

	/*
	 * For a relation RTE, we need only print the alias column names if any
	 * are different from the underlying "real" names.  For a function RTE,
	 * always emit a complete column alias list; this is to protect against
	 * possible instability of the default column names (eg, from altering
	 * parameter names).  For tablefunc RTEs, we never print aliases, because
	 * the column names are part of the clause itself.  For other RTE types,
	 * print if we changed anything OR if there were user-written column
	 * aliases (since the latter would be part of the underlying "reality").
	 */
	if (rte->rtekind == RTE_RELATION)
		colinfo->printaliases = changed_any;
	else if (rte->rtekind == RTE_FUNCTION)
		colinfo->printaliases = true;
	else if (rte->rtekind == RTE_TABLEFUNC)
		colinfo->printaliases = false;
	else if (rte->alias && rte->alias->colnames != NIL)
		colinfo->printaliases = true;
	else
		colinfo->printaliases = changed_any;
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

/* ----------
 * get_target_list			- Parse back a SELECT target list
 *
 * This is also used for RETURNING lists in INSERT/UPDATE/DELETE.
 *
 * resultDesc and colNamesVisible are as for get_query_def()
 * ----------
 */
static void
get_target_list(List *targetList, deparse_context *context,
				TupleDesc resultDesc, bool colNamesVisible)
{
	StringInfo	buf = context->buf;
	StringInfoData targetbuf;
	bool		last_was_multiline = false;
	char	   *sep;
	int			colno;
	ListCell   *l;

	/* we use targetbuf to hold each TLE's text temporarily */
	initStringInfo(&targetbuf);

	sep = " ";
	colno = 0;
	foreach(l, targetList)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(l);
		char	   *colname;
		char	   *attname;

		if (tle->resjunk)
			continue;			/* ignore junk entries */

		appendStringInfoString(buf, sep);
		sep = ", ";
		colno++;

		/*
		 * Put the new field text into targetbuf so we can decide after we've
		 * got it whether or not it needs to go on a new line.
		 */
		resetStringInfo(&targetbuf);
		context->buf = &targetbuf;

		/*
		 * We special-case Var nodes rather than using get_rule_expr. This is
		 * needed because get_rule_expr will display a whole-row Var as
		 * "foo.*", which is the preferred notation in most contexts, but at
		 * the top level of a SELECT list it's not right (the parser will
		 * expand that notation into multiple columns, yielding behavior
		 * different from a whole-row Var).  We need to call get_variable
		 * directly so that we can tell it to do the right thing, and so that
		 * we can get the attribute name which is the default AS label.
		 */
		if (tle->expr && (IsA(tle->expr, Var)))
		{
			attname = get_variable((Var *) tle->expr, 0, true, context);
		}
		else
		{
			get_rule_expr((Node *) tle->expr, context, true);

			/*
			 * When colNamesVisible is true, we should always show the
			 * assigned column name explicitly.  Otherwise, show it only if
			 * it's not FigureColname's fallback.
			 */
			attname = colNamesVisible ? NULL : "?column?";
		}

		/*
		 * Figure out what the result column should be called.  In the context
		 * of a view, use the view's tuple descriptor (so as to pick up the
		 * effects of any column RENAME that's been done on the view).
		 * Otherwise, just use what we can find in the TLE.
		 */
		if (resultDesc && colno <= resultDesc->natts)
			colname = NameStr(TupleDescAttr(resultDesc, colno - 1)->attname);
		else
			colname = tle->resname;

		/* Show AS unless the column's name is correct as-is */
		if (colname)			/* resname could be NULL */
		{
			if (attname == NULL || strcmp(attname, colname) != 0)
				appendStringInfo(&targetbuf, " AS %s", quote_identifier(colname));
		}

		/* Restore context's output buffer */
		context->buf = buf;

		/* Consider line-wrapping if enabled */
		if (PRETTY_INDENT(context) && context->wrapColumn >= 0)
		{
			int			leading_nl_pos;

			/* Does the new field start with a new line? */
			if (targetbuf.len > 0 && targetbuf.data[0] == '\n')
				leading_nl_pos = 0;
			else
				leading_nl_pos = -1;

			/* If so, we shouldn't add anything */
			if (leading_nl_pos >= 0)
			{
				/* instead, remove any trailing spaces currently in buf */
				removeStringInfoSpaces(buf);
			}
			else
			{
				char	   *trailing_nl;

				/* Locate the start of the current line in the output buffer */
				trailing_nl = strrchr(buf->data, '\n');
				if (trailing_nl == NULL)
					trailing_nl = buf->data;
				else
					trailing_nl++;

				/*
				 * Add a newline, plus some indentation, if the new field is
				 * not the first and either the new field would cause an
				 * overflow or the last field used more than one line.
				 */
				if (colno > 1 &&
					((strlen(trailing_nl) + targetbuf.len > context->wrapColumn) ||
					 last_was_multiline))
					appendContextKeyword(context, "", -PRETTYINDENT_STD,
										 PRETTYINDENT_STD, PRETTYINDENT_VAR);
			}

			/* Remember this field's multiline status for next iteration */
			last_was_multiline =
				(strchr(targetbuf.data + leading_nl_pos + 1, '\n') != NULL);
		}

		/* Add the new field */
		appendBinaryStringInfo(buf, targetbuf.data, targetbuf.len);
	}

	/* clean up */
	pfree(targetbuf.data);
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

	/*
	 * Never emit resulttype(arg) functional notation. A pg_proc entry could
	 * take precedence, and a resulttype in pg_temp would require schema
	 * qualification that format_type_with_typemod() would usually omit. We've
	 * standardized on arg::resulttype, but CAST(arg AS resulttype) notation
	 * would work fine.
	 */
	appendStringInfo(buf, "::%s",
					 format_type_with_typemod(resulttype, resulttypmod));
}

/* ----------
 * get_const_expr
 *
 *	Make a string representation of a Const
 *
 * showtype can be -1 to never show "::typename" decoration, or +1 to always
 * show it, or 0 to show it only if the constant wouldn't be assumed to be
 * the right type by default.
 *
 * If the Const's collation isn't default for its type, show that too.
 * We mustn't do this when showtype is -1 (since that means the caller will
 * print "::typename", and we can't put a COLLATE clause in between).  It's
 * caller's responsibility that collation isn't missed in such cases.
 * ----------
 */
static void
get_const_expr(Const *constval, deparse_context *context, int showtype)
{
	StringInfo	buf = context->buf;
	Oid			typoutput;
	bool		typIsVarlena;
	char	   *extval;
	bool		needlabel = false;

	if (constval->constisnull)
	{
		/*
		 * Always label the type of a NULL constant to prevent misdecisions
		 * about type when reparsing.
		 */
		appendStringInfoString(buf, "NULL");
		if (showtype >= 0)
		{
			appendStringInfo(buf, "::%s",
							 format_type_with_typemod(constval->consttype,
													  constval->consttypmod));
			get_const_collation(constval, context);
		}
		return;
	}

	getTypeOutputInfo(constval->consttype,
					  &typoutput, &typIsVarlena);

	extval = OidOutputFunctionCall(typoutput, constval->constvalue);

	switch (constval->consttype)
	{
		case INT4OID:

			/*
			 * INT4 can be printed without any decoration, unless it is
			 * negative; in that case print it as '-nnn'::integer to ensure
			 * that the output will re-parse as a constant, not as a constant
			 * plus operator.  In most cases we could get away with printing
			 * (-nnn) instead, because of the way that gram.y handles negative
			 * literals; but that doesn't work for INT_MIN, and it doesn't
			 * seem that much prettier anyway.
			 */
			if (extval[0] != '-')
				appendStringInfoString(buf, extval);
			else
			{
				appendStringInfo(buf, "'%s'", extval);
				needlabel = true;	/* we must attach a cast */
			}
			break;

		case NUMERICOID:

			/*
			 * NUMERIC can be printed without quotes if it looks like a float
			 * constant (not an integer, and not Infinity or NaN) and doesn't
			 * have a leading sign (for the same reason as for INT4).
			 */
			if (isdigit((unsigned char) extval[0]) &&
				strcspn(extval, "eE.") != strlen(extval))
			{
				appendStringInfoString(buf, extval);
			}
			else
			{
				appendStringInfo(buf, "'%s'", extval);
				needlabel = true;	/* we must attach a cast */
			}
			break;

		case BOOLOID:
			if (strcmp(extval, "t") == 0)
				appendStringInfoString(buf, "true");
			else
				appendStringInfoString(buf, "false");
			break;

		default:
			simple_quote_literal(buf, extval);
			break;
	}

	pfree(extval);

	if (showtype < 0)
		return;

	/*
	 * For showtype == 0, append ::typename unless the constant will be
	 * implicitly typed as the right type when it is read in.
	 *
	 * XXX this code has to be kept in sync with the behavior of the parser,
	 * especially make_const.
	 */
	switch (constval->consttype)
	{
		case BOOLOID:
		case UNKNOWNOID:
			/* These types can be left unlabeled */
			needlabel = false;
			break;
		case INT4OID:
			/* We determined above whether a label is needed */
			break;
		case NUMERICOID:

			/*
			 * Float-looking constants will be typed as numeric, which we
			 * checked above; but if there's a nondefault typmod we need to
			 * show it.
			 */
			needlabel |= (constval->consttypmod >= 0);
			break;
		default:
			needlabel = true;
			break;
	}
	if (needlabel || showtype > 0)
		appendStringInfo(buf, "::%s",
						 format_type_with_typemod(constval->consttype,
												  constval->consttypmod));

	get_const_collation(constval, context);
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

	/*
	 * Never emit resulttype(arg) functional notation. A pg_proc entry could
	 * take precedence, and a resulttype in pg_temp would require schema
	 * qualification that format_type_with_typemod() would usually omit. We've
	 * standardized on arg::resulttype, but CAST(arg AS resulttype) notation
	 * would work fine.
	 */
	appendStringInfo(buf, "::%s",
					 format_type_with_typemod(resulttype, resulttypmod));
}

/* ----------
 * get_const_expr
 *
 *	Make a string representation of a Const
 *
 * showtype can be -1 to never show "::typename" decoration, or +1 to always
 * show it, or 0 to show it only if the constant wouldn't be assumed to be
 * the right type by default.
 *
 * If the Const's collation isn't default for its type, show that too.
 * We mustn't do this when showtype is -1 (since that means the caller will
 * print "::typename", and we can't put a COLLATE clause in between).  It's
 * caller's responsibility that collation isn't missed in such cases.
 * ----------
 */
static void
get_const_expr(Const *constval, deparse_context *context, int showtype)
{
	StringInfo	buf = context->buf;
	Oid			typoutput;
	bool		typIsVarlena;
	char	   *extval;
	bool		needlabel = false;

	if (constval->constisnull)
	{
		/*
		 * Always label the type of a NULL constant to prevent misdecisions
		 * about type when reparsing.
		 */
		appendStringInfoString(buf, "NULL");
		if (showtype >= 0)
		{
			appendStringInfo(buf, "::%s",
							 format_type_with_typemod(constval->consttype,
													  constval->consttypmod));
			get_const_collation(constval, context);
		}
		return;
	}

	getTypeOutputInfo(constval->consttype,
					  &typoutput, &typIsVarlena);

	extval = OidOutputFunctionCall(typoutput, constval->constvalue);

	switch (constval->consttype)
	{
		case INT4OID:

			/*
			 * INT4 can be printed without any decoration, unless it is
			 * negative; in that case print it as '-nnn'::integer to ensure
			 * that the output will re-parse as a constant, not as a constant
			 * plus operator.  In most cases we could get away with printing
			 * (-nnn) instead, because of the way that gram.y handles negative
			 * literals; but that doesn't work for INT_MIN, and it doesn't
			 * seem that much prettier anyway.
			 */
			if (extval[0] != '-')
				appendStringInfoString(buf, extval);
			else
			{
				appendStringInfo(buf, "'%s'", extval);
				needlabel = true;	/* we must attach a cast */
			}
			break;

		case NUMERICOID:

			/*
			 * NUMERIC can be printed without quotes if it looks like a float
			 * constant (not an integer, and not Infinity or NaN) and doesn't
			 * have a leading sign (for the same reason as for INT4).
			 */
			if (isdigit((unsigned char) extval[0]) &&
				strcspn(extval, "eE.") != strlen(extval))
			{
				appendStringInfoString(buf, extval);
			}
			else
			{
				appendStringInfo(buf, "'%s'", extval);
				needlabel = true;	/* we must attach a cast */
			}
			break;

		case BOOLOID:
			if (strcmp(extval, "t") == 0)
				appendStringInfoString(buf, "true");
			else
				appendStringInfoString(buf, "false");
			break;

		default:
			simple_quote_literal(buf, extval);
			break;
	}

	pfree(extval);

	if (showtype < 0)
		return;

	/*
	 * For showtype == 0, append ::typename unless the constant will be
	 * implicitly typed as the right type when it is read in.
	 *
	 * XXX this code has to be kept in sync with the behavior of the parser,
	 * especially make_const.
	 */
	switch (constval->consttype)
	{
		case BOOLOID:
		case UNKNOWNOID:
			/* These types can be left unlabeled */
			needlabel = false;
			break;
		case INT4OID:
			/* We determined above whether a label is needed */
			break;
		case NUMERICOID:

			/*
			 * Float-looking constants will be typed as numeric, which we
			 * checked above; but if there's a nondefault typmod we need to
			 * show it.
			 */
			needlabel |= (constval->consttypmod >= 0);
			break;
		default:
			needlabel = true;
			break;
	}
	if (needlabel || showtype > 0)
		appendStringInfo(buf, "::%s",
						 format_type_with_typemod(constval->consttype,
												  constval->consttypmod));

	get_const_collation(constval, context);
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
 * Deparse a Var which references OUTER_VAR, INNER_VAR, or INDEX_VAR.  This
 * routine is actually a callback for resolve_special_varno, which handles
 * finding the correct TargetEntry.  We get the expression contained in that
 * TargetEntry and just need to deparse it, a job we can throw back on
 * get_rule_expr.
 */
static void
get_special_variable(Node *node, deparse_context *context, void *callback_arg)
{
	StringInfo	buf = context->buf;

	/*
	 * For a non-Var referent, force parentheses because our caller probably
	 * assumed a Var is a simple expression.
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
resolve_special_varno(Node *node, deparse_context *context,
					  rsv_callback callback, void *callback_arg)
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
	 * If varno is special, recurse.  (Don't worry about varnosyn; if we're
	 * here, we already decided not to use that.)
	 */
	if (var->varno == OUTER_VAR && dpns->outer_tlist)
	{
		TargetEntry *tle;
		deparse_namespace save_dpns;
		Bitmapset  *save_appendparents;

		tle = get_tle_by_resno(dpns->outer_tlist, var->varattno);
		if (!tle)
			elog(ERROR, "bogus varattno for OUTER_VAR var: %d", var->varattno);

		/*
		 * If we're descending to the first child of an Append or MergeAppend,
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
		resolve_special_varno((Node *) tle->expr, context,
							  callback, callback_arg);
		pop_child_plan(dpns, &save_dpns);
		return;
	}
	else if (var->varno == INDEX_VAR && dpns->index_tlist)
	{
		TargetEntry *tle;

		tle = get_tle_by_resno(dpns->index_tlist, var->varattno);
		if (!tle)
			elog(ERROR, "bogus varattno for INDEX_VAR var: %d", var->varattno);

		resolve_special_varno((Node *) tle->expr, context,
							  callback, callback_arg);
		return;
	}
	else if (var->varno < 1 || var->varno > list_length(dpns->rtable))
		elog(ERROR, "bogus varno: %d", var->varno);

	/* Not special.  Just invoke the callback. */
	(*callback) (node, context, callback_arg);
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
 * generate_opclass_name
 *		Compute the name to display for a opclass specified by OID
 *
 * The result includes all necessary quoting and schema-prefixing.
 */
char *
generate_opclass_name(Oid opclass)
{
	StringInfoData buf;

	initStringInfo(&buf);
	get_opclass_name(opclass, InvalidOid, &buf);

	return &buf.data[1];		/* get_opclass_name() prepends space */
}

/*
 * processIndirection - take care of array and subfield assignment
 *
 * We strip any top-level FieldStore or assignment SubscriptingRef nodes that
 * appear in the input, printing them as decoration for the base column
 * name (which we assume the caller just printed).  We might also need to
 * strip CoerceToDomain nodes, but only ones that appear above assignment
 * nodes.
 *
 * Returns the subexpression that's to be assigned.
 */
static Node *
processIndirection(Node *node, deparse_context *context)
{
	StringInfo	buf = context->buf;
	CoerceToDomain *cdomain = NULL;

	for (;;)
	{
		if (node == NULL)
			break;
		if (IsA(node, FieldStore))
		{
			FieldStore *fstore = (FieldStore *) node;
			Oid			typrelid;
			char	   *fieldname;

			/* lookup tuple type */
			typrelid = get_typ_typrelid(fstore->resulttype);
			if (!OidIsValid(typrelid))
				elog(ERROR, "argument type %s of FieldStore is not a tuple type",
					 format_type_be(fstore->resulttype));

			/*
			 * Print the field name.  There should only be one target field in
			 * stored rules.  There could be more than that in executable
			 * target lists, but this function cannot be used for that case.
			 */
			Assert(list_length(fstore->fieldnums) == 1);
			fieldname = get_attname(typrelid,
									linitial_int(fstore->fieldnums), false);
			appendStringInfo(buf, ".%s", quote_identifier(fieldname));

			/*
			 * We ignore arg since it should be an uninteresting reference to
			 * the target column or subcolumn.
			 */
			node = (Node *) linitial(fstore->newvals);
		}
		else if (IsA(node, SubscriptingRef))
		{
			SubscriptingRef *sbsref = (SubscriptingRef *) node;

			if (sbsref->refassgnexpr == NULL)
				break;

			printSubscripts(sbsref, context);

			/*
			 * We ignore refexpr since it should be an uninteresting reference
			 * to the target column or subcolumn.
			 */
			node = (Node *) sbsref->refassgnexpr;
		}
		else if (IsA(node, CoerceToDomain))
		{
			cdomain = (CoerceToDomain *) node;
			/* If it's an explicit domain coercion, we're done */
			if (cdomain->coercionformat != COERCE_IMPLICIT_CAST)
				break;
			/* Tentatively descend past the CoerceToDomain */
			node = (Node *) cdomain->arg;
		}
		else
			break;
	}

	/*
	 * If we descended past a CoerceToDomain whose argument turned out not to
	 * be a FieldStore or array assignment, back up to the CoerceToDomain.
	 * (This is not enough to be fully correct if there are nested implicit
	 * CoerceToDomains, but such cases shouldn't ever occur.)
	 */
	if (cdomain && node == (Node *) cdomain->arg)
		node = (Node *) cdomain;

	return node;
}

static void
printSubscripts(SubscriptingRef *sbsref, deparse_context *context)
{
	StringInfo	buf = context->buf;
	ListCell   *lowlist_item;
	ListCell   *uplist_item;

	lowlist_item = list_head(sbsref->reflowerindexpr);	/* could be NULL */
	foreach(uplist_item, sbsref->refupperindexpr)
	{
		appendStringInfoChar(buf, '[');
		if (lowlist_item)
		{
			/* If subexpression is NULL, get_rule_expr prints nothing */
			get_rule_expr((Node *) lfirst(lowlist_item), context, false);
			appendStringInfoChar(buf, ':');
			lowlist_item = lnext(sbsref->reflowerindexpr, lowlist_item);
		}
		/* If subexpression is NULL, get_rule_expr prints nothing */
		get_rule_expr((Node *) lfirst(uplist_item), context, false);
		appendStringInfoChar(buf, ']');
	}
}

/*
 * quote_identifier			- Quote an identifier only if needed
 *
 * When quotes are needed, we palloc the required space; slightly
 * space-wasteful but well worth it for notational simplicity.
 */
const char *
quote_identifier(const char *ident)
{
	/*
	 * Can avoid quoting if ident starts with a lowercase letter or underscore
	 * and contains only lowercase letters, digits, and underscores, *and* is
	 * not any SQL keyword.  Otherwise, supply quotes.
	 */
	int			nquotes = 0;
	bool		safe;
	const char *ptr;
	char	   *result;
	char	   *optr;

	/*
	 * would like to use <ctype.h> macros here, but they might yield unwanted
	 * locale-specific results...
	 */
	safe = ((ident[0] >= 'a' && ident[0] <= 'z') || ident[0] == '_');

	for (ptr = ident; *ptr; ptr++)
	{
		char		ch = *ptr;

		if ((ch >= 'a' && ch <= 'z') ||
			(ch >= '0' && ch <= '9') ||
			(ch == '_'))
		{
			/* okay */
		}
		else
		{
			safe = false;
			if (ch == '"')
				nquotes++;
		}
	}

	if (quote_all_identifiers)
		safe = false;

	if (safe)
	{
		/*
		 * Check for keyword.  We quote keywords except for unreserved ones.
		 * (In some cases we could avoid quoting a col_name or type_func_name
		 * keyword, but it seems much harder than it's worth to tell that.)
		 *
		 * Note: ScanKeywordLookup() does case-insensitive comparison, but
		 * that's fine, since we already know we have all-lower-case.
		 */
		int			kwnum = ScanKeywordLookup(ident, &ScanKeywords);

		if (kwnum >= 0 && ScanKeywordCategories[kwnum] != UNRESERVED_KEYWORD)
			safe = false;
	}

	if (safe)
		return ident;			/* no change needed */

	result = (char *) palloc(strlen(ident) + nquotes + 2 + 1);

	optr = result;
	*optr++ = '"';
	for (ptr = ident; *ptr; ptr++)
	{
		char		ch = *ptr;

		if (ch == '"')
			*optr++ = '"';
		*optr++ = ch;
	}
	*optr++ = '"';
	*optr = '\0';

	return result;
}

/*
 * quote_qualified_identifier	- Quote a possibly-qualified identifier
 *
 * Return a name of the form qualifier.ident, or just ident if qualifier
 * is NULL, quoting each component if necessary.  The result is palloc'd.
 */
char *
quote_qualified_identifier(const char *qualifier,
						   const char *ident)
{
	StringInfoData buf;

	initStringInfo(&buf);
	if (qualifier)
		appendStringInfo(&buf, "%s.", quote_identifier(qualifier));
	appendStringInfoString(&buf, quote_identifier(ident));
	return buf.data;
}

/*
 * get_relation_name
 *		Get the unqualified name of a relation specified by OID
 *
 * This differs from the underlying get_rel_name() function in that it will
 * throw error instead of silently returning NULL if the OID is bad.
 */
static char *
get_relation_name(Oid relid)
{
	char	   *relname = get_rel_name(relid);

	if (!relname)
		elog(ERROR, "cache lookup failed for relation %u", relid);
	return relname;
}

/* ----------
 * get_from_clause			- Parse back a FROM clause
 *
 * "prefix" is the keyword that denotes the start of the list of FROM
 * elements. It is FROM when used to parse back SELECT and UPDATE, but
 * is USING when parsing back DELETE.
 * ----------
 */
static void
get_from_clause(Query *query, const char *prefix, deparse_context *context)
{
	StringInfo	buf = context->buf;
	bool		first = true;
	ListCell   *l;

	/*
	 * We use the query's jointree as a guide to what to print.  However, we
	 * must ignore auto-added RTEs that are marked not inFromCl. (These can
	 * only appear at the top level of the jointree, so it's sufficient to
	 * check here.)  This check also ensures we ignore the rule pseudo-RTEs
	 * for NEW and OLD.
	 */
	foreach(l, query->jointree->fromlist)
	{
		Node	   *jtnode = (Node *) lfirst(l);

		if (IsA(jtnode, RangeTblRef))
		{
			int			varno = ((RangeTblRef *) jtnode)->rtindex;
			RangeTblEntry *rte = rt_fetch(varno, query->rtable);

			if (!rte->inFromCl)
				continue;
		}

		if (first)
		{
			appendContextKeyword(context, prefix,
								 -PRETTYINDENT_STD, PRETTYINDENT_STD, 2);
			first = false;

			get_from_clause_item(jtnode, query, context);
		}
		else
		{
			StringInfoData itembuf;

			appendStringInfoString(buf, ", ");

			/*
			 * Put the new FROM item's text into itembuf so we can decide
			 * after we've got it whether or not it needs to go on a new line.
			 */
			initStringInfo(&itembuf);
			context->buf = &itembuf;

			get_from_clause_item(jtnode, query, context);

			/* Restore context's output buffer */
			context->buf = buf;

			/* Consider line-wrapping if enabled */
			if (PRETTY_INDENT(context) && context->wrapColumn >= 0)
			{
				/* Does the new item start with a new line? */
				if (itembuf.len > 0 && itembuf.data[0] == '\n')
				{
					/* If so, we shouldn't add anything */
					/* instead, remove any trailing spaces currently in buf */
					removeStringInfoSpaces(buf);
				}
				else
				{
					char	   *trailing_nl;

					/* Locate the start of the current line in the buffer */
					trailing_nl = strrchr(buf->data, '\n');
					if (trailing_nl == NULL)
						trailing_nl = buf->data;
					else
						trailing_nl++;

					/*
					 * Add a newline, plus some indentation, if the new item
					 * would cause an overflow.
					 */
					if (strlen(trailing_nl) + itembuf.len > context->wrapColumn)
						appendContextKeyword(context, "", -PRETTYINDENT_STD,
											 PRETTYINDENT_STD,
											 PRETTYINDENT_VAR);
				}
			}

			/* Add the new item */
			appendBinaryStringInfo(buf, itembuf.data, itembuf.len);

			/* clean up */
			pfree(itembuf.data);
		}
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
				remaining_ma_columns = count_nonjunk_tlist_entries(((Query *) cur_ma_sublink->subselect)->targetList);
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
 * Detect whether query looks like SELECT ... FROM VALUES(),
 * with no need to rename the output columns of the VALUES RTE.
 * If so, return the VALUES RTE.  Otherwise return NULL.
 */
static RangeTblEntry *
get_simple_values_rte(Query *query, TupleDesc resultDesc)
{
	RangeTblEntry *result = NULL;
	ListCell   *lc;

	/*
	 * We want to detect a match even if the Query also contains OLD or NEW
	 * rule RTEs.  So the idea is to scan the rtable and see if there is only
	 * one inFromCl RTE that is a VALUES RTE.
	 */
	foreach(lc, query->rtable)
	{
		RangeTblEntry *rte = (RangeTblEntry *) lfirst(lc);

		if (rte->rtekind == RTE_VALUES && rte->inFromCl)
		{
			if (result)
				return NULL;	/* multiple VALUES (probably not possible) */
			result = rte;
		}
		else if (rte->rtekind == RTE_RELATION && !rte->inFromCl)
			continue;			/* ignore rule entries */
		else
			return NULL;		/* something else -> not simple VALUES */
	}

	/*
	 * We don't need to check the targetlist in any great detail, because
	 * parser/analyze.c will never generate a "bare" VALUES RTE --- they only
	 * appear inside auto-generated sub-queries with very restricted
	 * structure.  However, DefineView might have modified the tlist by
	 * injecting new column aliases, or we might have some other column
	 * aliases forced by a resultDesc.  We can only simplify if the RTE's
	 * column names match the names that get_target_list() would select.
	 */
	if (result)
	{
		ListCell   *lcn;
		int			colno;

		if (list_length(query->targetList) != list_length(result->eref->colnames))
			return NULL;		/* this probably cannot happen */
		colno = 0;
		forboth(lc, query->targetList, lcn, result->eref->colnames)
		{
			TargetEntry *tle = (TargetEntry *) lfirst(lc);
			char	   *cname = strVal(lfirst(lcn));
			char	   *colname;

			if (tle->resjunk)
				return NULL;	/* this probably cannot happen */

			/* compute name that get_target_list would use for column */
			colno++;
			if (resultDesc && colno <= resultDesc->natts)
				colname = NameStr(TupleDescAttr(resultDesc, colno - 1)->attname);
			else
				colname = tle->resname;

			/* does it match the VALUES RTE? */
			if (colname == NULL || strcmp(colname, cname) != 0)
				return NULL;	/* column name has been changed */
		}
	}

	return result;
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
 * Display a Param appropriately.
 */
static void
get_parameter(Param *param, deparse_context *context)
{
	Node	   *expr;
	deparse_namespace *dpns;
	ListCell   *ancestor_cell;

	/*
	 * If it's a PARAM_EXEC parameter, try to locate the expression from which
	 * the parameter was computed.  Note that failing to find a referent isn't
	 * an error, since the Param might well be a subplan output rather than an
	 * input.
	 */
	expr = find_param_referent(param, context, &dpns, &ancestor_cell);
	if (expr)
	{
		/* Found a match, so print it */
		deparse_namespace save_dpns;
		bool		save_varprefix;
		bool		need_paren;

		/* Switch attention to the ancestor plan node */
		push_ancestor_plan(dpns, ancestor_cell, &save_dpns);

		/*
		 * Force prefixing of Vars, since they won't belong to the relation
		 * being scanned in the original plan node.
		 */
		save_varprefix = context->varprefix;
		context->varprefix = true;

		/*
		 * A Param's expansion is typically a Var, Aggref, GroupingFunc, or
		 * upper-level Param, which wouldn't need extra parentheses.
		 * Otherwise, insert parens to ensure the expression looks atomic.
		 */
		need_paren = !(IsA(expr, Var) ||
					   IsA(expr, Aggref) ||
					   IsA(expr, GroupingFunc) ||
					   IsA(expr, Param));
		if (need_paren)
			appendStringInfoChar(context->buf, '(');

		get_rule_expr(expr, context, false);

		if (need_paren)
			appendStringInfoChar(context->buf, ')');

		context->varprefix = save_varprefix;

		pop_ancestor_plan(dpns, &save_dpns);

		return;
	}

	/*
	 * If it's an external parameter, see if the outermost namespace provides
	 * function argument names.
	 */
	if (param->paramkind == PARAM_EXTERN && context->namespaces != NIL)
	{
		dpns = llast(context->namespaces);
		if (dpns->argnames &&
			param->paramid > 0 &&
			param->paramid <= dpns->numargs)
		{
			char	   *argname = dpns->argnames[param->paramid - 1];

			if (argname)
			{
				bool		should_qualify = false;
				ListCell   *lc;

				/*
				 * Qualify the parameter name if there are any other deparse
				 * namespaces with range tables.  This avoids qualifying in
				 * trivial cases like "RETURN a + b", but makes it safe in all
				 * other cases.
				 */
				foreach(lc, context->namespaces)
				{
					deparse_namespace *dpns = lfirst(lc);

					if (list_length(dpns->rtable_names) > 0)
					{
						should_qualify = true;
						break;
					}
				}
				if (should_qualify)
				{
					appendStringInfoString(context->buf, quote_identifier(dpns->funcname));
					appendStringInfoChar(context->buf, '.');
				}

				appendStringInfoString(context->buf, quote_identifier(argname));
				return;
			}
		}
	}

	/*
	 * Not PARAM_EXEC, or couldn't find referent: just print $N.
	 */
	appendStringInfo(context->buf, "$%d", param->paramid);
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

void
inline_cte(PlannerInfo *root, CommonTableExpr *cte)
{
	struct inline_cte_walker_context context;

	context.ctename = cte->ctename;
	/* Start at levelsup = -1 because we'll immediately increment it */
	context.levelsup = -1;
	context.ctequery = castNode(Query, cte->ctequery);

	(void) inline_cte_walker((Node *) root->parse, &context);
}

static bool
inline_cte_walker(Node *node, inline_cte_walker_context *context)
{
	if (node == NULL)
		return false;
	if (IsA(node, Query))
	{
		Query	   *query = (Query *) node;

		context->levelsup++;

		/*
		 * Visit the query's RTE nodes after their contents; otherwise
		 * query_tree_walker would descend into the newly inlined CTE query,
		 * which we don't want.
		 */
		(void) query_tree_walker(query, inline_cte_walker, context,
								 QTW_EXAMINE_RTES_AFTER);

		context->levelsup--;

		return false;
	}
	else if (IsA(node, RangeTblEntry))
	{
		RangeTblEntry *rte = (RangeTblEntry *) node;

		if (rte->rtekind == RTE_CTE &&
			strcmp(rte->ctename, context->ctename) == 0 &&
			rte->ctelevelsup == context->levelsup)
		{
			/*
			 * Found a reference to replace.  Generate a copy of the CTE query
			 * with appropriate level adjustment for outer references (e.g.,
			 * to other CTEs).
			 */
			Query	   *newquery = copyObject(context->ctequery);

			if (context->levelsup > 0)
				IncrementVarSublevelsUp((Node *) newquery, context->levelsup, 1);

			/*
			 * Convert the RTE_CTE RTE into a RTE_SUBQUERY.
			 *
			 * Historically, a FOR UPDATE clause has been treated as extending
			 * into views and subqueries, but not into CTEs.  We preserve this
			 * distinction by not trying to push rowmarks into the new
			 * subquery.
			 */
			rte->rtekind = RTE_SUBQUERY;
			rte->subquery = newquery;
			rte->security_barrier = false;

			/* Zero out CTE-specific fields */
			rte->ctename = NULL;
			rte->ctelevelsup = 0;
			rte->self_reference = false;
			rte->coltypes = NIL;
			rte->coltypmods = NIL;
			rte->colcollations = NIL;
		}

		return false;
	}

	return expression_tree_walker(node, inline_cte_walker, context);
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

/* ----------
 * get_utility_query_def			- Parse back a UTILITY parsetree
 * ----------
 */
static void
get_utility_query_def(Query *query, deparse_context *context)
{
	StringInfo	buf = context->buf;

	if (query->utilityStmt && IsA(query->utilityStmt, NotifyStmt))
	{
		NotifyStmt *stmt = (NotifyStmt *) query->utilityStmt;

		appendContextKeyword(context, "",
							 0, PRETTYINDENT_STD, 1);
		appendStringInfo(buf, "NOTIFY %s",
						 quote_identifier(stmt->conditionname));
		if (stmt->payload)
		{
			appendStringInfoString(buf, ", ");
			simple_quote_literal(buf, stmt->payload);
		}
	}
	else
	{
		/* Currently only NOTIFY utility commands can appear in rules */
		elog(ERROR, "unexpected utility statement type");
	}
}
