static bool
contain_mutable_functions_walker(Node *node, void *context)
{
	if (node == NULL)
		return false;

	/* Check for mutable functions in node itself */
	if (check_functions_in_node(node, contain_mutable_functions_checker,
								context))
		return true;

#if PG_VERSION_NUM < 160000

	if (IsA(node, SQLValueFunction))
	{
		/* all variants of SQLValueFunction are stable */
		return true;
	}

#endif

	if (IsA(node, NextValueExpr))
	{
		/* NextValueExpr is volatile */
		return true;
	}

	/*
	 * It should be safe to treat MinMaxExpr as immutable, because it will
	 * depend on a non-cross-type btree comparison function, and those should
	 * always be immutable.  Treating XmlExpr as immutable is more dubious,
	 * and treating CoerceToDomain as immutable is outright dangerous.  But we
	 * have done so historically, and changing this would probably cause more
	 * problems than it would fix.  In practice, if you have a non-immutable
	 * domain constraint you are in for pain anyhow.
	 */

	/* Recurse to check arguments */
	if (IsA(node, Query))
	{
		/* Recurse into subselects */
		return query_tree_walker((Query *) node,
								 contain_mutable_functions_walker,
								 context, 0);
	}
	return expression_tree_walker(node, contain_mutable_functions_walker,
								  context);
}