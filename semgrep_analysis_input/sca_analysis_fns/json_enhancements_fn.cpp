static void
array_to_json_internal(Datum array, StringInfo result, bool use_line_feeds)
{
	ArrayType  *v = DatumGetArrayTypeP(array);
	Oid			element_type = ARR_ELEMTYPE(v);
	int		   *dim;
	int			ndim;
	int			nitems;
	int			count = 0;
	Datum	   *elements;
	bool	   *nulls;
	int16		typlen;
	bool		typbyval;
	char        typalign;
	JsonTypeCategory tcategory;
	Oid         outfuncoid;

	ndim = ARR_NDIM(v);
	dim = ARR_DIMS(v);
	nitems = ArrayGetNItems(ndim, dim);

	if (nitems <= 0)
	{
		appendStringInfoString(result, "[]");
		return;
	}

	get_typlenbyvalalign(element_type,
						 &typlen, &typbyval, &typalign);
 
	json_categorize_type(element_type,
						 &tcategory, &outfuncoid);
 
	deconstruct_array(v, element_type, typlen, typbyval,
					  typalign, &elements, &nulls,
					  &nitems);

	array_dim_to_json(result, 0, ndim, dim, elements, nulls, &count, tcategory,
					  outfuncoid, use_line_feeds);

	pfree(elements);
	pfree(nulls);
}