/*
 * Process a single dimension of an array.
 * If it's the innermost dimension, output the values, otherwise call
 * ourselves recursively to process the next dimension.
 */
static void
array_dim_to_json(StringInfo result, int dim, int ndims, int *dims, Datum *vals,
				  bool *nulls, int *valcount, JsonTypeCategory tcategory,
				  Oid outfuncoid, bool use_line_feeds)
{
	int			i;
	const char *sep;

	Assert(dim < ndims);

	sep = use_line_feeds ? ",\n " : ",";

	appendStringInfoChar(result, '[');

	for (i = 1; i <= dims[dim]; i++)
	{
		if (i > 1)
			appendStringInfoString(result, sep);

		if (dim + 1 == ndims)
		{
			datum_to_json(vals[*valcount], nulls[*valcount], result, tcategory,
						  outfuncoid, false);
			(*valcount)++;
		}
		else
		{
			/*
			 * Do we want line feeds on inner dimensions of arrays? For now
			 * we'll say no.
			 */
			array_dim_to_json(result, dim + 1, ndims, dims, vals, nulls,
							  valcount, tcategory, outfuncoid, false);
		}
	}

	appendStringInfoChar(result, ']');
}

/*
 * Turn an array into JSON.
 */
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
	char		typalign;
	JsonTypeCategory tcategory;
	Oid			outfuncoid;

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

/*
 * Deconstruct the text representation of a 1-dimensional Postgres array
 * into individual items.
 *
 * On success, returns true and sets *itemarray and *nitems to describe
 * an array of individual strings.  On parse failure, returns false;
 * *itemarray may exist or be NULL.
 *
 * NOTE: free'ing itemarray is sufficient to deallocate the working storage.
 */
bool
parsePGArray(const char *atext, char ***itemarray, int *nitems)
{
	int			inputlen;
	char	  **items;
	char	   *strings;
	int			curitem;

	/*
	 * We expect input in the form of "{item,item,item}" where any item is
	 * either raw data, or surrounded by double quotes (in which case embedded
	 * characters including backslashes and quotes are backslashed).
	 *
	 * We build the result as an array of pointers followed by the actual
	 * string data, all in one malloc block for convenience of deallocation.
	 * The worst-case storage need is not more than one pointer and one
	 * character for each input character (consider "{,,,,,,,,,,}").
	 */
	*itemarray = NULL;
	*nitems = 0;
	inputlen = strlen(atext);
	if (inputlen < 2 || atext[0] != '{' || atext[inputlen - 1] != '}')
		return false;			/* bad input */
	items = (char **) malloc(inputlen * (sizeof(char *) + sizeof(char)));
	if (items == NULL)
		return false;			/* out of memory */
	*itemarray = items;
	strings = (char *) (items + inputlen);

	atext++;					/* advance over initial '{' */
	curitem = 0;
	while (*atext != '}')
	{
		if (*atext == '\0')
			return false;		/* premature end of string */
		items[curitem] = strings;
		while (*atext != '}' && *atext != ',')
		{
			if (*atext == '\0')
				return false;	/* premature end of string */
			if (*atext != '"')
				*strings++ = *atext++;	/* copy unquoted data */
			else
			{
				/* process quoted substring */
				atext++;
				while (*atext != '"')
				{
					if (*atext == '\0')
						return false;	/* premature end of string */
					if (*atext == '\\')
					{
						atext++;
						if (*atext == '\0')
							return false;		/* premature end of string */
					}
					*strings++ = *atext++;		/* copy quoted data */
				}
				atext++;
			}
		}
		*strings++ = '\0';
		if (*atext == ',')
			atext++;
		curitem++;
	}
	if (atext[1] != '\0')
		return false;			/* bogus syntax (embedded '}') */
	*nitems = curitem;
	return true;
}

/*
 * Given an array of dependency references, eliminate any duplicates.
 */
static void
eliminate_duplicate_dependencies(ObjectAddresses *addrs)
{
	ObjectAddress *priorobj;
	int			oldref,
				newrefs;

	/*
	 * We can't sort if the array has "extra" data, because there's no way to
	 * keep it in sync.  Fortunately that combination of features is not
	 * needed.
	 */
	Assert(!addrs->extras);

	if (addrs->numrefs <= 1)
		return;					/* nothing to do */

	/* Sort the refs so that duplicates are adjacent */
	qsort((void *) addrs->refs, addrs->numrefs, sizeof(ObjectAddress),
		  object_address_comparator);

	/* Remove dups */
	priorobj = addrs->refs;
	newrefs = 1;
	for (oldref = 1; oldref < addrs->numrefs; oldref++)
	{
		ObjectAddress *thisobj = addrs->refs + oldref;

		if (priorobj->classId == thisobj->classId &&
			priorobj->objectId == thisobj->objectId)
		{
			if (priorobj->objectSubId == thisobj->objectSubId)
				continue;		/* identical, so drop thisobj */

			/*
			 * If we have a whole-object reference and a reference to a part
			 * of the same object, we don't need the whole-object reference
			 * (for example, we don't need to reference both table foo and
			 * column foo.bar).  The whole-object reference will always appear
			 * first in the sorted list.
			 */
			if (priorobj->objectSubId == 0)
			{
				/* replace whole ref with partial */
				priorobj->objectSubId = thisobj->objectSubId;
				continue;
			}
		}
		/* Not identical, so add thisobj to output set */
		priorobj++;
		*priorobj = *thisobj;
		newrefs++;
	}

	addrs->numrefs = newrefs;
}

/*
 * Add an entry to an ObjectAddresses array.
 *
 * As above, but specify entry exactly and provide some "extra" data too.
 */
static void
add_exact_object_address_extra(const ObjectAddress *object,
							   const ObjectAddressExtra *extra,
							   ObjectAddresses *addrs)
{
	ObjectAddress *item;
	ObjectAddressExtra *itemextra;

	/* allocate extra space if first time */
	if (!addrs->extras)
		addrs->extras = (ObjectAddressExtra *)
			palloc(addrs->maxrefs * sizeof(ObjectAddressExtra));

	/* enlarge array if needed */
	if (addrs->numrefs >= addrs->maxrefs)
	{
		addrs->maxrefs *= 2;
		addrs->refs = (ObjectAddress *)
			repalloc(addrs->refs, addrs->maxrefs * sizeof(ObjectAddress));
		addrs->extras = (ObjectAddressExtra *)
			repalloc(addrs->extras, addrs->maxrefs * sizeof(ObjectAddressExtra));
	}
	/* record this item */
	item = addrs->refs + addrs->numrefs;
	*item = *object;
	itemextra = addrs->extras + addrs->numrefs;
	*itemextra = *extra;
	addrs->numrefs++;
}