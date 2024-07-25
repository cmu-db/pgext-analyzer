/*
 * Extract data from the pg_statistic arrays into useful format.
 */
static Selectivity
mcelem_tsquery_selec(TSQuery query, Datum *mcelem, int nmcelem,
					 float4 *numbers, int nnumbers)
{
	float4		minfreq;
	TextFreq   *lookup;
	Selectivity selec;
	int			i;

	/*
	 * There should be two more Numbers than Values, because the last two
	 * cells are taken for minimal and maximal frequency.  Punt if not.
	 *
	 * (Note: the MCELEM statistics slot definition allows for a third extra
	 * number containing the frequency of nulls, but we're not expecting that
	 * to appear for a tsvector2 column.)
	 */
	if (nnumbers != nmcelem + 2)
		return tsquery_opr_selec_no_stats(query);

	/*
	 * Transpose the data into a single array so we can use bsearch().
	 */
	lookup = (TextFreq *) palloc(sizeof(TextFreq) * nmcelem);
	for (i = 0; i < nmcelem; i++)
	{
		/*
		 * The text Datums came from an array, so it cannot be compressed or
		 * stored out-of-line -- it's safe to use VARSIZE_ANY*.
		 */
		Assert(!VARATT_IS_COMPRESSED(mcelem[i]) && !VARATT_IS_EXTERNAL(mcelem[i]));
		lookup[i].element = (text *) DatumGetPointer(mcelem[i]);
		lookup[i].frequency = numbers[i];
	}

	/*
	 * Grab the lowest frequency. compute_tsvector_stats() stored it for us in
	 * the one before the last cell of the Numbers array. See ts_typanalyze.c
	 */
	minfreq = numbers[nnumbers - 2];

	selec = tsquery_opr_selec(GETQUERY(query), GETOPERAND(query), lookup,
							  nmcelem, minfreq);

	pfree(lookup);

	return selec;
}

/*
 * Traverse the tsquery in preorder, calculating selectivity as:
 *
 *	 selec(left_oper) * selec(right_oper) in AND & PHRASE nodes,
 *
 *	 selec(left_oper) + selec(right_oper) -
 *		selec(left_oper) * selec(right_oper) in OR nodes,
 *
 *	 1 - select(oper) in NOT nodes
 *
 *	 histogram-based estimation in prefix VAL nodes
 *
 *	 freq[val] in exact VAL nodes, if the value is in MCELEM
 *	 min(freq[MCELEM]) / 2 in VAL nodes, if it is not
 *
 * The MCELEM array is already sorted (see ts_typanalyze.c), so we can use
 * binary search for determining freq[MCELEM].
 *
 * If we don't have stats for the tsvector2, we still use this logic,
 * except we use default estimates for VAL nodes.  This case is signaled
 * by lookup == NULL.
 */
static Selectivity
tsquery_opr_selec(QueryItem *item, char *operand,
				  TextFreq *lookup, int length, float4 minfreq)
{
	Selectivity selec;

	/* since this function recurses, it could be driven to stack overflow */
	check_stack_depth();

	if (item->type == QI_VAL)
	{
		QueryOperand *oper = (QueryOperand *) item;
		LexemeKey	key;

		/*
		 * Prepare the key for bsearch().
		 */
		key.lexeme = operand + oper->distance;
		key.length = oper->length;

		if (oper->prefix)
		{
			/* Prefix match, ie the query item is lexeme:* */
			Selectivity matched,
						allmces;
			int			i,
						n_matched;

			/*
			 * Our strategy is to scan through the MCELEM list and combine the
			 * frequencies of the ones that match the prefix.  We then
			 * extrapolate the fraction of matching MCELEMs to the remaining
			 * rows, assuming that the MCELEMs are representative of the whole
			 * lexeme population in this respect.  (Compare
			 * histogram_selectivity().)  Note that these are most common
			 * elements not most common values, so they're not mutually
			 * exclusive.  We treat occurrences as independent events.
			 *
			 * This is only a good plan if we have a pretty fair number of
			 * MCELEMs available; we set the threshold at 100.  If no stats or
			 * insufficient stats, arbitrarily use DEFAULT_TS_MATCH_SEL*4.
			 */
			if (lookup == NULL || length < 100)
				return (Selectivity) (DEFAULT_TS_MATCH_SEL * 4);

			matched = allmces = 0;
			n_matched = 0;
			for (i = 0; i < length; i++)
			{
				TextFreq   *t = lookup + i;
				int			tlen = VARSIZE_ANY_EXHDR(t->element);

				if (tlen >= key.length &&
					strncmp(key.lexeme, VARDATA_ANY(t->element),
							key.length) == 0)
				{
					matched += t->frequency - matched * t->frequency;
					n_matched++;
				}
				allmces += t->frequency - allmces * t->frequency;
			}

			/* Clamp to ensure sanity in the face of roundoff error */
			CLAMP_PROBABILITY(matched);
			CLAMP_PROBABILITY(allmces);

			selec = matched + (1.0 - allmces) * ((double) n_matched / length);

			/*
			 * In any case, never believe that a prefix match has selectivity
			 * less than we would assign for a non-MCELEM lexeme.  This
			 * preserves the property that "word:*" should be estimated to
			 * match at least as many rows as "word" would be.
			 */
			selec = Max(Min(DEFAULT_TS_MATCH_SEL, minfreq / 2), selec);
		}
		else
		{
			/* Regular exact lexeme match */
			TextFreq   *searchres;

			/* If no stats for the variable, use DEFAULT_TS_MATCH_SEL */
			if (lookup == NULL)
				return (Selectivity) DEFAULT_TS_MATCH_SEL;

			searchres = (TextFreq *) bsearch(&key, lookup, length,
											 sizeof(TextFreq),
											 compare_lexeme_textfreq);

			if (searchres)
			{
				/*
				 * The element is in MCELEM.  Return precise selectivity (or
				 * at least as precise as ANALYZE could find out).
				 */
				selec = searchres->frequency;
			}
			else
			{
				/*
				 * The element is not in MCELEM.  Punt, but assume that the
				 * selectivity cannot be more than minfreq / 2.
				 */
				selec = Min(DEFAULT_TS_MATCH_SEL, minfreq / 2);
			}
		}
	}
	else
	{
		/* Current TSQuery node is an operator */
		Selectivity s1,
					s2;

		switch (item->qoperator.oper)
		{
			case OP_NOT:
				selec = 1.0 - tsquery_opr_selec(item + 1, operand,
												lookup, length, minfreq);
				break;

			case OP_PHRASE:
			case OP_AND:
				s1 = tsquery_opr_selec(item + 1, operand,
									   lookup, length, minfreq);
				s2 = tsquery_opr_selec(item + item->qoperator.left, operand,
									   lookup, length, minfreq);
				selec = s1 * s2;
				break;

			case OP_OR:
				s1 = tsquery_opr_selec(item + 1, operand,
									   lookup, length, minfreq);
				s2 = tsquery_opr_selec(item + item->qoperator.left, operand,
									   lookup, length, minfreq);
				selec = s1 + s2 - s1 * s2;
				break;

			default:
				elog(ERROR, "unrecognized operator: %d", item->qoperator.oper);
				selec = 0;		/* keep compiler quiet */
				break;
		}
	}

	/* Clamp intermediate results to stay sane despite roundoff error */
	CLAMP_PROBABILITY(selec);

	return selec;
}

/*
 * bsearch() comparator for a lexeme (non-NULL terminated string with length)
 * and a TextFreq. Use length, then byte-for-byte comparison, because that's
 * how ANALYZE code sorted data before storing it in a statistic tuple.
 * See ts_typanalyze.c for details.
 */
static int
compare_lexeme_textfreq(const void *e1, const void *e2)
{
	const LexemeKey *key = (const LexemeKey *) e1;
	const TextFreq *t = (const TextFreq *) e2;
	int			len1,
				len2;

	len1 = key->length;
	len2 = VARSIZE_ANY_EXHDR(t->element);

	/* Compare lengths first, possibly avoiding a strncmp call */
	if (len1 > len2)
		return 1;
	else if (len1 < len2)
		return -1;

	/* Fall back on byte-for-byte comparison */
	return strncmp(key->lexeme, VARDATA_ANY(t->element), len1);
}

#define MAXQROPOS	MAXENTRYPOS
typedef struct
{
	bool		operandexists;
	bool		reverseinsert;	/* indicates insert order, true means
								 * descending order */
	uint32		npos;
	WordEntryPos pos[MAXQROPOS];
} QueryRepresentationOperand;

typedef struct
{
	TSQuery		query;
	QueryRepresentationOperand *operandData;
} QueryRepresentation;

#define QR_GET_OPERAND_DATA(q, v) \
	( (q)->operandData + (((QueryItem*)(v)) - GETQUERY((q)->query)) )

static TSTernaryValue
checkcondition_QueryOperand(void *checkval, QueryOperand *val, ExecPhraseData *data)
{
	QueryRepresentation *qr = (QueryRepresentation *) checkval;
	QueryRepresentationOperand *opData = QR_GET_OPERAND_DATA(qr, val);

	if (!opData->operandexists)
		return TS_NO;

	if (data)
	{
		data->npos = opData->npos;
		data->pos = opData->pos;
		if (opData->reverseinsert)
			data->pos += MAXQROPOS - opData->npos;
	}

	return TS_YES;
}

typedef struct
{
	int			pos;
	int			p;
	int			q;
	DocRepresentation *begin;
	DocRepresentation *end;
} CoverExt;

static void
resetQueryRepresentation(QueryRepresentation *qr, bool reverseinsert)
{
	int			i;

	for (i = 0; i < qr->query->size; i++)
	{
		qr->operandData[i].operandexists = false;
		qr->operandData[i].reverseinsert = reverseinsert;
		qr->operandData[i].npos = 0;
	}
}

static void
fillQueryRepresentationData(QueryRepresentation *qr, DocRepresentation *entry)
{
	int			i;
	int			lastPos;
	QueryRepresentationOperand *opData;

	for (i = 0; i < entry->data.query.nitem; i++)
	{
		if (entry->data.query.items[i]->type != QI_VAL)
			continue;

		opData = QR_GET_OPERAND_DATA(qr, entry->data.query.items[i]);

		opData->operandexists = true;

		if (opData->npos == 0)
		{
			lastPos = (opData->reverseinsert) ? (MAXQROPOS - 1) : 0;
			opData->pos[lastPos] = entry->pos;
			opData->npos++;
			continue;
		}

		lastPos = opData->reverseinsert ?
			(MAXQROPOS - opData->npos) :
			(opData->npos - 1);

		if (WEP_GETPOS(opData->pos[lastPos]) != WEP_GETPOS(entry->pos))
		{
			lastPos = opData->reverseinsert ?
				(MAXQROPOS - 1 - opData->npos) :
				(opData->npos);

			opData->pos[lastPos] = entry->pos;
			opData->npos++;
		}
	}
}

static int
uniqueWORD(ParsedWord *a, int32 l)
{
	ParsedWord *ptr,
			   *res;
	int			tmppos;

	if (l == 1)
	{
		tmppos = LIMITPOS(a->pos.pos);
		a->alen = 2;
		a->pos.apos = (uint16 *) palloc(sizeof(uint16) * a->alen);
		a->pos.apos[0] = 1;
		a->pos.apos[1] = tmppos;
		return l;
	}

	res = a;
	ptr = a + 1;

	/*
	 * Sort words with its positions
	 */
	qsort((void *) a, l, sizeof(ParsedWord), compareWORD);

	/*
	 * Initialize first word and its first position
	 */
	tmppos = LIMITPOS(a->pos.pos);
	a->alen = 2;
	a->pos.apos = (uint16 *) palloc(sizeof(uint16) * a->alen);
	a->pos.apos[0] = 1;
	a->pos.apos[1] = tmppos;

	/*
	 * Summarize position information for each word
	 */
	while (ptr - a < l)
	{
		if (!(ptr->len == res->len &&
			  strncmp(ptr->word, res->word, res->len) == 0))
		{
			/*
			 * Got a new word, so put it in result
			 */
			res++;
			res->len = ptr->len;
			res->word = ptr->word;
			tmppos = LIMITPOS(ptr->pos.pos);
			res->alen = 2;
			res->pos.apos = (uint16 *) palloc(sizeof(uint16) * res->alen);
			res->pos.apos[0] = 1;
			res->pos.apos[1] = tmppos;
		}
		else
		{
			/*
			 * The word already exists, so adjust position information. But
			 * before we should check size of position's array, max allowed
			 * value for position and uniqueness of position
			 */
			pfree(ptr->word);
			if (res->pos.apos[0] < MAXNUMPOS - 1 && res->pos.apos[res->pos.apos[0]] != MAXENTRYPOS - 1 &&
				res->pos.apos[res->pos.apos[0]] != LIMITPOS(ptr->pos.pos))
			{
				if (res->pos.apos[0] + 1 >= res->alen)
				{
					res->alen *= 2;
					res->pos.apos = (uint16 *) repalloc(res->pos.apos, sizeof(uint16) * res->alen);
				}
				if (res->pos.apos[0] == 0 || res->pos.apos[res->pos.apos[0]] != LIMITPOS(ptr->pos.pos))
				{
					res->pos.apos[res->pos.apos[0] + 1] = LIMITPOS(ptr->pos.pos);
					res->pos.apos[0]++;
				}
			}
		}
		ptr++;
	}

	return res + 1 - a;
}

static StatEntry *
walkStatEntryTree(TSVectorStat *stat)
{
	StatEntry  *node = stat->stack[stat->stackpos];

	if (node == NULL)
		return NULL;

	if (node->ndoc != 0)
	{
		/* return entry itself: we already was at left sublink */
		return node;
	}
	else if (node->right && node->right != stat->stack[stat->stackpos + 1])
	{
		/* go on right sublink */
		stat->stackpos++;
		node = node->right;

		/* find most-left value */
		for (;;)
		{
			stat->stack[stat->stackpos] = node;
			if (node->left)
			{
				stat->stackpos++;
				node = node->left;
			}
			else
				break;
		}
		Assert(stat->stackpos <= stat->maxdepth);
	}
	else
	{
		/* we already return all left subtree, itself and  right subtree */
		if (stat->stackpos == 0)
			return NULL;

		stat->stackpos--;
		return walkStatEntryTree(stat);
	}

	return node;
}

static Datum
ts_process_call(FuncCallContext *funcctx)
{
	TSVectorStat *st;
	StatEntry  *entry;

	st = (TSVectorStat *) funcctx->user_fctx;

	entry = walkStatEntryTree(st);

	if (entry != NULL)
	{
		Datum		result;
		char	   *values[3];
		char		ndoc[16];
		char		nentry[16];
		HeapTuple	tuple;

		values[0] = palloc(entry->lenlexeme + 1);
		memcpy(values[0], entry->lexeme, entry->lenlexeme);
		(values[0])[entry->lenlexeme] = '\0';
		sprintf(ndoc, "%d", entry->ndoc);
		values[1] = ndoc;
		sprintf(nentry, "%d", entry->nentry);
		values[2] = nentry;

		tuple = BuildTupleFromCStrings(funcctx->attinmeta, values);
		result = HeapTupleGetDatum(tuple);

		pfree(values[0]);

		/* mark entry as already visited */
		entry->ndoc = 0;

		return result;
	}

	return (Datum) 0;
}

static const float *
getWeights(ArrayType *win)
{
	static float ws[lengthof(weights)];
	int			i;
	float4	   *arrdata;

	if (win == NULL)
		return weights;

	if (ARR_NDIM(win) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
				 errmsg("array of weight must be one-dimensional")));

	if (ArrayGetNItems(ARR_NDIM(win), ARR_DIMS(win)) < lengthof(weights))
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
				 errmsg("array of weight is too short")));

	if (array_contains_nulls(win))
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("array of weight must not contain nulls")));

	arrdata = (float4 *) ARR_DATA_PTR(win);
	for (i = 0; i < lengthof(weights); i++)
	{
		ws[i] = (arrdata[i] >= 0) ? arrdata[i] : weights[i];
		if (ws[i] > 1.0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("weight out of range")));
	}

	return ws;
}

/*
 * Returns a sorted, de-duplicated array of QueryOperands in a query.
 * The returned QueryOperands are pointers to the original QueryOperands
 * in the query.
 *
 * Length of the returned array is stored in *size
 */
static QueryOperand **
SortAndUniqItems(TSQuery q, int *size)
{
	char	   *operand = GETOPERAND(q);
	QueryItem  *item = GETQUERY(q);
	QueryOperand **res,
			  **ptr,
			  **prevptr;

	ptr = res = (QueryOperand **) palloc(sizeof(QueryOperand *) * *size);

	/* Collect all operands from the tree to res */
	while ((*size)--)
	{
		if (item->type == QI_VAL)
		{
			*ptr = (QueryOperand *) item;
			ptr++;
		}
		item++;
	}

	*size = ptr - res;
	if (*size < 2)
		return res;

	qsort_arg(res, *size, sizeof(QueryOperand *), compareQueryOperand, (void *) operand);

	ptr = res + 1;
	prevptr = res;

	/* remove duplicates */
	while (ptr - res < *size)
	{
		if (compareQueryOperand((void *) ptr, (void *) prevptr, (void *) operand) != 0)
		{
			prevptr++;
			*prevptr = *ptr;
		}
		ptr++;
	}

	*size = prevptr + 1 - res;
	return res;
}

/* Compare two WordEntryPos values for qsort */
int
compareWordEntryPos(const void *a, const void *b)
{
	int			apos = WEP_GETPOS(*(const WordEntryPos *) a);
	int			bpos = WEP_GETPOS(*(const WordEntryPos *) b);

	if (apos == bpos)
		return 0;
	return (apos > bpos) ? 1 : -1;
}

/*
 * Removes duplicate pos entries. If there's two entries with same pos
 * but different weight, the higher weight is retained.
 *
 * Returns new length.
 */
static int
uniquePos(WordEntryPos *a, int l)
{
	WordEntryPos *ptr,
			   *res;

	if (l <= 1)
		return l;

	qsort((void *) a, l, sizeof(WordEntryPos), compareWordEntryPos);

	res = a;
	ptr = a + 1;
	while (ptr - a < l)
	{
		if (WEP_GETPOS(*ptr) != WEP_GETPOS(*res))
		{
			res++;
			*res = *ptr;
			if (res - a >= MAXNUMPOS - 1 ||
				WEP_GETPOS(*res) == MAXENTRYPOS - 1)
				break;
		}
		else if (WEP_GETWEIGHT(*ptr) > WEP_GETWEIGHT(*res))
			WEP_SETWEIGHT(*res, WEP_GETWEIGHT(*ptr));
		ptr++;
	}

	return res + 1 - a;
}

/*
 * qsort comparator functions
 */

static int
compare_int(const void *va, const void *vb)
{
	int			a = *((const int *) va);
	int			b = *((const int *) vb);

	if (a == b)
		return 0;
	return (a > b) ? 1 : -1;
}

Datum
tsvector2_matchjoinsel(PG_FUNCTION_ARGS)
{
	/* for the moment we just punt */
	PG_RETURN_FLOAT8(DEFAULT_TS_MATCH_SEL);
}

/*
 * @@ selectivity for tsvector2 var vs tsquery constant
 */
static Selectivity
tsquerysel(VariableStatData *vardata, Datum constval)
{
	Selectivity selec;
	TSQuery		query;

	/* The caller made sure the const is a TSQuery, so get it now */
	query = DatumGetTSQuery(constval);

	/* Empty query matches nothing */
	if (query->size == 0)
		return (Selectivity) 0.0;

	if (HeapTupleIsValid(vardata->statsTuple))
	{
		Form_pg_statistic stats;
		AttStatsSlot sslot;

		stats = (Form_pg_statistic) GETSTRUCT(vardata->statsTuple);

		/* MCELEM will be an array of TEXT elements for a tsvector2 column */
		if (get_attstatsslot(&sslot, vardata->statsTuple,
							 STATISTIC_KIND_MCELEM, InvalidOid,
							 ATTSTATSSLOT_VALUES | ATTSTATSSLOT_NUMBERS))
		{
			/*
			 * There is a most-common-elements slot for the tsvector2 Var, so
			 * use that.
			 */
			selec = mcelem_tsquery_selec(query, sslot.values, sslot.nvalues,
										 sslot.numbers, sslot.nnumbers);
			free_attstatsslot(&sslot);
		}
		else
		{
			/* No most-common-elements info, so do without */
			selec = tsquery_opr_selec_no_stats(query);
		}

		/*
		 * MCE stats count only non-null rows, so adjust for null rows.
		 */
		selec *= (1.0 - stats->stanullfrac);
	}
	else
	{
		/* No stats at all, so do without */
		selec = tsquery_opr_selec_no_stats(query);
		/* we assume no nulls here, so no stanullfrac correction */
	}

	return selec;
}

Datum
tsvector2_stat2(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	Datum		result;

	if (SRF_IS_FIRSTCALL())
	{
		TSVectorStat *stat;
		text	   *txt = PG_GETARG_TEXT_PP(0);
		text	   *ws = PG_GETARG_TEXT_PP(1);

		funcctx = SRF_FIRSTCALL_INIT();
		SPI_connect();
		stat = ts_stat_sql(funcctx->multi_call_memory_ctx, txt, ws);
		PG_FREE_IF_COPY(txt, 0);
		PG_FREE_IF_COPY(ws, 1);
		ts_setup_firstcall(fcinfo, funcctx, stat);
		SPI_finish();
	}

	funcctx = SRF_PERCALL_SETUP();
	if ((result = ts_process_call(funcctx)) != (Datum) 0)
		SRF_RETURN_NEXT(funcctx, result);
	SRF_RETURN_DONE(funcctx);
}

/*
 * Detect whether a tsquery boolean expression requires any positive matches
 * to values shown in the tsquery.
 *
 * This is needed to know whether a GIN index search requires full index scan.
 * For example, 'x & !y' requires a match of x, so it's sufficient to scan
 * entries for x; but 'x | !y' could match rows containing neither x nor y.
 */
bool
tsquery_requires_match(QueryItem *curitem)
{
	/* since this function recurses, it could be driven to stack overflow */
	check_stack_depth();

	if (curitem->type == QI_VAL)
		return true;

	switch (curitem->qoperator.oper)
	{
		case OP_NOT:

			/*
			 * Assume there are no required matches underneath a NOT.  For
			 * some cases with nested NOTs, we could prove there's a required
			 * match, but it seems unlikely to be worth the trouble.
			 */
			return false;

		case OP_PHRASE:

			/*
			 * Treat OP_PHRASE as OP_AND here
			 */
		case OP_AND:
			/* If either side requires a match, we're good */
			if (tsquery_requires_match(curitem + curitem->qoperator.left))
				return true;
			else
				return tsquery_requires_match(curitem + 1);

		case OP_OR:
			/* Both sides must require a match */
			if (tsquery_requires_match(curitem + curitem->qoperator.left))
				return tsquery_requires_match(curitem + 1);
			else
				return false;

		default:
			elog(ERROR, "unrecognized operator: %d", curitem->qoperator.oper);
	}

	/* not reachable, but keep compiler quiet */
	return false;
}

static void
add_to_tsvector2(void *_state, char *elem_value, int elem_len)
{
	TSVectorBuildState *state = (TSVectorBuildState *) _state;
	ParsedText *prs = state->prs;
	int32		prevwords;

	if (prs->words == NULL)
	{
		/*
		 * First time through: initialize words array to a reasonable size.
		 * (parsetext() will realloc it bigger as needed.)
		 */
		prs->lenwords = 16;
		prs->words = (ParsedWord *) palloc(sizeof(ParsedWord) * prs->lenwords);
		prs->curwords = 0;
		prs->pos = 0;
	}

	prevwords = prs->curwords;

	parsetext(state->cfgId, prs, elem_value, elem_len);

	/*
	 * If we extracted any words from this JSON element, advance pos to create
	 * an artificial break between elements.  This is because we don't want
	 * phrase searches to think that the last word in this element is adjacent
	 * to the first word in the next one.
	 */
	if (prs->curwords > prevwords)
		prs->pos += 1;
}

Datum
tsvector2_stat1(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	Datum		result;

	if (SRF_IS_FIRSTCALL())
	{
		TSVectorStat *stat;
		text	   *txt = PG_GETARG_TEXT_PP(0);

		funcctx = SRF_FIRSTCALL_INIT();
		SPI_connect();
		stat = ts_stat_sql(funcctx->multi_call_memory_ctx, txt, NULL);
		PG_FREE_IF_COPY(txt, 0);
		ts_setup_firstcall(fcinfo, funcctx, stat);
		SPI_finish();
	}

	funcctx = SRF_PERCALL_SETUP();
	if ((result = ts_process_call(funcctx)) != (Datum) 0)
		SRF_RETURN_NEXT(funcctx, result);
	SRF_RETURN_DONE(funcctx);
}