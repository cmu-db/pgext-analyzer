void
jsonb_put_escaped_value(StringInfo out, JsonbValue * scalarVal)
{
	switch (scalarVal->type)
	{
		case jbvNull:
			appendBinaryStringInfo(out, "null", 4);
			break;
		case jbvString:
			escape_json(out, pnstrdup(scalarVal->val.string.val, scalarVal->val.string.len));
			break;
		case jbvNumeric:
			appendStringInfoString(out,
								   DatumGetCString(DirectFunctionCall1(numeric_out,
																	   PointerGetDatum(scalarVal->val.numeric))));
			break;
		case jbvBool:
			if (scalarVal->val.boolean)
				appendBinaryStringInfo(out, "true", 4);
			else
				appendBinaryStringInfo(out, "false", 5);
			break;
		default:
			elog(ERROR, "unknown jsonb scalar type");
	}
}