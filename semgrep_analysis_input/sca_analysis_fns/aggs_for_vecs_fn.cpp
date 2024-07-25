Datum
vec_to_first_finalfn(PG_FUNCTION_ARGS)
{
  Datum result;
  ArrayBuildState *state;
  int dims[1];
  int lbs[1];

  Assert(AggCheckCallContext(fcinfo, NULL));

  state = PG_ARGISNULL(0) ? NULL : (ArrayBuildState *)PG_GETARG_POINTER(0);

  if (state == NULL)
    PG_RETURN_NULL();

  dims[0] = state->nelems;
  lbs[0] = 1;

  result = makeMdArrayResult(state, 1, dims, lbs, CurrentMemoryContext, false);
  PG_RETURN_DATUM(result);
}