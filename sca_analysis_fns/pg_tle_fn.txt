/*
 * pg_extension_config_dump
 *
 * Record information about a configuration table that belongs to an
 * extension being created, but whose contents should be dumped in whole
 * or in part during pg_dump.
 */
Datum
pg_extension_config_dump(PG_FUNCTION_ARGS)
{
	Oid			tableoid = PG_GETARG_OID(0);
	text	   *wherecond = PG_GETARG_TEXT_PP(1);
	char	   *tablename;
	Relation	extRel;
	ScanKeyData key[1];
	SysScanDesc extScan;
	HeapTuple	extTup;
	Datum		arrayDatum;
	Datum		elementDatum;
	int			arrayLength;
	int			arrayIndex;
	bool		isnull;
	Datum		repl_val[Natts_pg_extension];
	bool		repl_null[Natts_pg_extension];
	bool		repl_repl[Natts_pg_extension];
	ArrayType  *a;

	/*
	 * We only allow this to be called from an extension's SQL script. We
	 * shouldn't need any permissions check beyond that.
	 */
	if (!creating_extension)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("%s can only be called from an SQL script executed by CREATE EXTENSION",
						"pg_extension_config_dump()")));

	/*
	 * Check that the table exists and is a member of the extension being
	 * created.  This ensures that we don't need to register an additional
	 * dependency to protect the extconfig entry.
	 */
	tablename = get_rel_name(tableoid);
	if (tablename == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_TABLE),
				 errmsg("OID %u does not refer to a table", tableoid)));
	if (getExtensionOfObject(RelationRelationId, tableoid) !=
		CurrentExtensionObject)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("table \"%s\" is not a member of the extension being created",
						tablename)));

	/*
	 * Add the table OID and WHERE condition to the extension's extconfig and
	 * extcondition arrays.
	 *
	 * If the table is already in extconfig, treat this as an update of the
	 * WHERE condition.
	 */

	/* Find the pg_extension tuple */
	extRel = table_open(ExtensionRelationId, RowExclusiveLock);

	ScanKeyInit(&key[0],
				Anum_pg_extension_oid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(CurrentExtensionObject));

	extScan = systable_beginscan(extRel, ExtensionOidIndexId, true,
								 NULL, 1, key);

	extTup = systable_getnext(extScan);

	if (!HeapTupleIsValid(extTup))	/* should not happen */
		elog(ERROR, "could not find tuple for extension %u",
			 CurrentExtensionObject);

	memset(repl_val, 0, sizeof(repl_val));
	memset(repl_null, false, sizeof(repl_null));
	memset(repl_repl, false, sizeof(repl_repl));

	/* Build or modify the extconfig value */
	elementDatum = ObjectIdGetDatum(tableoid);

	arrayDatum = heap_getattr(extTup, Anum_pg_extension_extconfig,
							  RelationGetDescr(extRel), &isnull);
	if (isnull)
	{
		/* Previously empty extconfig, so build 1-element array */
		arrayLength = 0;
		arrayIndex = 1;

		a = construct_array(&elementDatum, 1,
							OIDOID,
							sizeof(Oid), true, TYPALIGN_INT);
	}
	else
	{
		/* Modify or extend existing extconfig array */
		Oid		   *arrayData;
		int			i;

		a = DatumGetArrayTypeP(arrayDatum);

		arrayLength = ARR_DIMS(a)[0];
		if (ARR_NDIM(a) != 1 ||
			ARR_LBOUND(a)[0] != 1 ||
			arrayLength < 0 ||
			ARR_HASNULL(a) ||
			ARR_ELEMTYPE(a) != OIDOID)
			elog(ERROR, "extconfig is not a 1-D Oid array");
		arrayData = (Oid *) ARR_DATA_PTR(a);

		arrayIndex = arrayLength + 1;	/* set up to add after end */

		for (i = 0; i < arrayLength; i++)
		{
			if (arrayData[i] == tableoid)
			{
				arrayIndex = i + 1; /* replace this element instead */
				break;
			}
		}

		a = array_set(a, 1, &arrayIndex,
					  elementDatum,
					  false,
					  -1 /* varlena array */ ,
					  sizeof(Oid) /* OID's typlen */ ,
					  true /* OID's typbyval */ ,
					  TYPALIGN_INT /* OID's typalign */ );
	}
	repl_val[Anum_pg_extension_extconfig - 1] = PointerGetDatum(a);
	repl_repl[Anum_pg_extension_extconfig - 1] = true;

	/* Build or modify the extcondition value */
	elementDatum = PointerGetDatum(wherecond);

	arrayDatum = heap_getattr(extTup, Anum_pg_extension_extcondition,
							  RelationGetDescr(extRel), &isnull);
	if (isnull)
	{
		if (arrayLength != 0)
			elog(ERROR, "extconfig and extcondition arrays do not match");

		a = construct_array(&elementDatum, 1,
							TEXTOID,
							-1, false, TYPALIGN_INT);
	}
	else
	{
		a = DatumGetArrayTypeP(arrayDatum);

		if (ARR_NDIM(a) != 1 ||
			ARR_LBOUND(a)[0] != 1 ||
			ARR_HASNULL(a) ||
			ARR_ELEMTYPE(a) != TEXTOID)
			elog(ERROR, "extcondition is not a 1-D text array");
		if (ARR_DIMS(a)[0] != arrayLength)
			elog(ERROR, "extconfig and extcondition arrays do not match");

		/* Add or replace at same index as in extconfig */
		a = array_set(a, 1, &arrayIndex,
					  elementDatum,
					  false,
					  -1 /* varlena array */ ,
					  -1 /* TEXT's typlen */ ,
					  false /* TEXT's typbyval */ ,
					  TYPALIGN_INT /* TEXT's typalign */ );
	}
	repl_val[Anum_pg_extension_extcondition - 1] = PointerGetDatum(a);
	repl_repl[Anum_pg_extension_extcondition - 1] = true;

	extTup = heap_modify_tuple(extTup, RelationGetDescr(extRel),
							   repl_val, repl_null, repl_repl);

	CatalogTupleUpdate(extRel, &extTup->t_self, extTup);

	systable_endscan(extScan);

	table_close(extRel, RowExclusiveLock);

	PG_RETURN_VOID();
}

/*
 * extension_config_remove
 *
 * Remove the specified table OID from extension's extconfig, if present.
 * This is not currently exposed as a function, but it could be;
 * for now, we just invoke it from ALTER EXTENSION DROP.
 */
static void
extension_config_remove(Oid extensionoid, Oid tableoid)
{
	Relation	extRel;
	ScanKeyData key[1];
	SysScanDesc extScan;
	HeapTuple	extTup;
	Datum		arrayDatum;
	int			arrayLength;
	int			arrayIndex;
	bool		isnull;
	Datum		repl_val[Natts_pg_extension];
	bool		repl_null[Natts_pg_extension];
	bool		repl_repl[Natts_pg_extension];
	ArrayType  *a;

	/* Find the pg_extension tuple */
	extRel = table_open(ExtensionRelationId, RowExclusiveLock);

	ScanKeyInit(&key[0],
				Anum_pg_extension_oid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(extensionoid));

	extScan = systable_beginscan(extRel, ExtensionOidIndexId, true,
								 NULL, 1, key);

	extTup = systable_getnext(extScan);

	if (!HeapTupleIsValid(extTup))	/* should not happen */
		elog(ERROR, "could not find tuple for extension %u",
			 extensionoid);

	/* Search extconfig for the tableoid */
	arrayDatum = heap_getattr(extTup, Anum_pg_extension_extconfig,
							  RelationGetDescr(extRel), &isnull);
	if (isnull)
	{
		/* nothing to do */
		a = NULL;
		arrayLength = 0;
		arrayIndex = -1;
	}
	else
	{
		Oid		   *arrayData;
		int			i;

		a = DatumGetArrayTypeP(arrayDatum);

		arrayLength = ARR_DIMS(a)[0];
		if (ARR_NDIM(a) != 1 ||
			ARR_LBOUND(a)[0] != 1 ||
			arrayLength < 0 ||
			ARR_HASNULL(a) ||
			ARR_ELEMTYPE(a) != OIDOID)
			elog(ERROR, "extconfig is not a 1-D Oid array");
		arrayData = (Oid *) ARR_DATA_PTR(a);

		arrayIndex = -1;		/* flag for no deletion needed */

		for (i = 0; i < arrayLength; i++)
		{
			if (arrayData[i] == tableoid)
			{
				arrayIndex = i; /* index to remove */
				break;
			}
		}
	}

	/* If tableoid is not in extconfig, nothing to do */
	if (arrayIndex < 0)
	{
		systable_endscan(extScan);
		table_close(extRel, RowExclusiveLock);
		return;
	}

	/* Modify or delete the extconfig value */
	memset(repl_val, 0, sizeof(repl_val));
	memset(repl_null, false, sizeof(repl_null));
	memset(repl_repl, false, sizeof(repl_repl));

	if (arrayLength <= 1)
	{
		/* removing only element, just set array to null */
		repl_null[Anum_pg_extension_extconfig - 1] = true;
	}
	else
	{
		/* squeeze out the target element */
		Datum	   *dvalues;
		int			nelems;
		int			i;

		/* We already checked there are no nulls */
		deconstruct_array(a, OIDOID, sizeof(Oid), true, TYPALIGN_INT,
						  &dvalues, NULL, &nelems);

		for (i = arrayIndex; i < arrayLength - 1; i++)
			dvalues[i] = dvalues[i + 1];

		a = construct_array(dvalues, arrayLength - 1,
							OIDOID, sizeof(Oid), true, TYPALIGN_INT);

		repl_val[Anum_pg_extension_extconfig - 1] = PointerGetDatum(a);
	}
	repl_repl[Anum_pg_extension_extconfig - 1] = true;

	/* Modify or delete the extcondition value */
	arrayDatum = heap_getattr(extTup, Anum_pg_extension_extcondition,
							  RelationGetDescr(extRel), &isnull);
	if (isnull)
	{
		elog(ERROR, "extconfig and extcondition arrays do not match");
	}
	else
	{
		a = DatumGetArrayTypeP(arrayDatum);

		if (ARR_NDIM(a) != 1 ||
			ARR_LBOUND(a)[0] != 1 ||
			ARR_HASNULL(a) ||
			ARR_ELEMTYPE(a) != TEXTOID)
			elog(ERROR, "extcondition is not a 1-D text array");
		if (ARR_DIMS(a)[0] != arrayLength)
			elog(ERROR, "extconfig and extcondition arrays do not match");
	}

	if (arrayLength <= 1)
	{
		/* removing only element, just set array to null */
		repl_null[Anum_pg_extension_extcondition - 1] = true;
	}
	else
	{
		/* squeeze out the target element */
		Datum	   *dvalues;
		int			nelems;
		int			i;

		/* We already checked there are no nulls */
		deconstruct_array(a, TEXTOID, -1, false, TYPALIGN_INT,
						  &dvalues, NULL, &nelems);

		for (i = arrayIndex; i < arrayLength - 1; i++)
			dvalues[i] = dvalues[i + 1];

		a = construct_array(dvalues, arrayLength - 1,
							TEXTOID, -1, false, TYPALIGN_INT);

		repl_val[Anum_pg_extension_extcondition - 1] = PointerGetDatum(a);
	}
	repl_repl[Anum_pg_extension_extcondition - 1] = true;

	extTup = heap_modify_tuple(extTup, RelationGetDescr(extRel),
							   repl_val, repl_null, repl_repl);

	CatalogTupleUpdate(extRel, &extTup->t_self, extTup);

	systable_endscan(extScan);

	table_close(extRel, RowExclusiveLock);
}

/*
 * Apply a series of update scripts as though individual ALTER EXTENSION
 * UPDATE commands had been given, including altering the pg_extension row
 * and dependencies each time.
 *
 * This might be more work than necessary, but it ensures that old update
 * scripts don't break if newer versions have different control parameters.
 */
static void
ApplyExtensionUpdates(Oid extensionOid,
					  ExtensionControlFile *pcontrol,
					  const char *initialVersion,
					  List *updateVersions,
					  char *origSchemaName,
					  bool cascade,
					  bool is_create)
{
	const char *oldVersionName = initialVersion;
	ListCell   *lcv;

	foreach(lcv, updateVersions)
	{
		char	   *versionName = (char *) lfirst(lcv);
		ExtensionControlFile *control;
		char	   *schemaName;
		Oid			schemaOid;
		List	   *requiredExtensions;
		List	   *requiredSchemas;
		Relation	extRel;
		ScanKeyData key[1];
		SysScanDesc extScan;
		HeapTuple	extTup;
		Form_pg_extension extForm;
		Datum		values[Natts_pg_extension];
		bool		nulls[Natts_pg_extension];
		bool		repl[Natts_pg_extension];
		ObjectAddress myself;
		ListCell   *lc;

		/*
		 * Fetch parameters for specific version (pcontrol is not changed)
		 */
		control = read_extension_aux_control_file(pcontrol, versionName);

		/* Find the pg_extension tuple */
		extRel = table_open(ExtensionRelationId, RowExclusiveLock);

		ScanKeyInit(&key[0],
					Anum_pg_extension_oid,
					BTEqualStrategyNumber, F_OIDEQ,
					ObjectIdGetDatum(extensionOid));

		extScan = systable_beginscan(extRel, ExtensionOidIndexId, true,
									 NULL, 1, key);

		extTup = systable_getnext(extScan);

		if (!HeapTupleIsValid(extTup))	/* should not happen */
			elog(ERROR, "could not find tuple for extension %u",
				 extensionOid);

		extForm = (Form_pg_extension) GETSTRUCT(extTup);

		/*
		 * Determine the target schema (set by original install)
		 */
		schemaOid = extForm->extnamespace;
		schemaName = get_namespace_name(schemaOid);

		/*
		 * Modify extrelocatable and extversion in the pg_extension tuple
		 */
		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));
		memset(repl, 0, sizeof(repl));

		values[Anum_pg_extension_extrelocatable - 1] =
			BoolGetDatum(control->relocatable);
		repl[Anum_pg_extension_extrelocatable - 1] = true;
		values[Anum_pg_extension_extversion - 1] =
			CStringGetTextDatum(versionName);
		repl[Anum_pg_extension_extversion - 1] = true;

		extTup = heap_modify_tuple(extTup, RelationGetDescr(extRel),
								   values, nulls, repl);

		CatalogTupleUpdate(extRel, &extTup->t_self, extTup);

		systable_endscan(extScan);

		table_close(extRel, RowExclusiveLock);

		/*
		 * Look up the prerequisite extensions for this version, install them
		 * if necessary, and build lists of their OIDs and the OIDs of their
		 * target schemas.
		 */
		requiredExtensions = NIL;
		requiredSchemas = NIL;
		foreach(lc, control->requires)
		{
			char	   *curreq = (char *) lfirst(lc);
			Oid			reqext;
			Oid			reqschema;

			reqext = get_required_extension(curreq,
											control->name,
											origSchemaName,
											cascade,
											NIL,
											is_create);
			reqschema = get_extension_schema(reqext);
			requiredExtensions = lappend_oid(requiredExtensions, reqext);
			requiredSchemas = lappend_oid(requiredSchemas, reqschema);
		}

		/*
		 * Remove and recreate dependencies on prerequisite extensions
		 */
		deleteDependencyRecordsForClass(ExtensionRelationId, extensionOid,
										ExtensionRelationId,
										DEPENDENCY_NORMAL);

		myself.classId = ExtensionRelationId;
		myself.objectId = extensionOid;
		myself.objectSubId = 0;

		foreach(lc, requiredExtensions)
		{
			Oid			reqext = lfirst_oid(lc);
			ObjectAddress otherext;

			otherext.classId = ExtensionRelationId;
			otherext.objectId = reqext;
			otherext.objectSubId = 0;

			recordDependencyOn(&myself, &otherext, DEPENDENCY_NORMAL);
		}

		InvokeObjectPostAlterHook(ExtensionRelationId, extensionOid, 0);

		/*
		 * Finally, execute the update script file
		 */
		execute_extension_script(extensionOid, control,
								 oldVersionName, versionName,
								 requiredSchemas,
								 schemaName, schemaOid);

		/*
		 * Update prior-version name and loop around.  Since
		 * execute_sql_string did a final CommandCounterIncrement, we can
		 * update the pg_extension row again.
		 */
		oldVersionName = versionName;
	}
}

/*
 * Inner loop for pg_available_extension_versions:
 *		read versions of one extension, add rows to tupstore
 */
static void
get_available_versions_for_extension(ExtensionControlFile *pcontrol,
									 Tuplestorestate *tupstore,
									 TupleDesc tupdesc)
{
	List	   *evi_list;
	ListCell   *lc;

	/* Extract the version update graph from the script directory */
	evi_list = get_ext_ver_list(pcontrol);

	/* For each installable version ... */
	foreach(lc, evi_list)
	{
		ExtensionVersionInfo *evi = (ExtensionVersionInfo *) lfirst(lc);
		ExtensionControlFile *control;
		Datum		values[8];
		bool		nulls[8];
		ListCell   *lc2;

		if (!evi->installable)
			continue;

		/*
		 * Fetch parameters for specific version (pcontrol is not changed)
		 */
		control = read_extension_aux_control_file(pcontrol, evi->name);

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		/* name */
		values[0] = DirectFunctionCall1(namein,
										CStringGetDatum(control->name));
		/* version */
		values[1] = CStringGetTextDatum(evi->name);
		/* superuser */
		values[2] = BoolGetDatum(control->superuser);
		/* trusted */
		values[3] = BoolGetDatum(control->trusted);
		/* relocatable */
		values[4] = BoolGetDatum(control->relocatable);
		/* schema */
		if (control->schema == NULL)
			nulls[5] = true;
		else
			values[5] = DirectFunctionCall1(namein,
											CStringGetDatum(control->schema));
		/* requires */
		if (control->requires == NIL)
			nulls[6] = true;
		else
			values[6] = convert_requires_to_datum(control->requires);
		/* comment */
		if (control->comment == NULL)
			nulls[7] = true;
		else
			values[7] = CStringGetTextDatum(control->comment);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);

		/*
		 * Find all non-directly-installable versions that would be installed
		 * starting from this version, and report them, inheriting the
		 * parameters that aren't changed in updates from this version.
		 */
		foreach(lc2, evi_list)
		{
			ExtensionVersionInfo *evi2 = (ExtensionVersionInfo *) lfirst(lc2);
			List	   *best_path;

			if (evi2->installable)
				continue;
			if (find_install_path(evi_list, evi2, &best_path) == evi)
			{
				/*
				 * Fetch parameters for this version (pcontrol is not changed)
				 */
				control = read_extension_aux_control_file(pcontrol, evi2->name);

				/* name stays the same */
				/* version */
				values[1] = CStringGetTextDatum(evi2->name);
				/* superuser */
				values[2] = BoolGetDatum(control->superuser);
				/* trusted */
				values[3] = BoolGetDatum(control->trusted);
				/* relocatable */
				values[4] = BoolGetDatum(control->relocatable);
				/* schema stays the same */
				/* requires */
				if (control->requires == NIL)
					nulls[6] = true;
				else
				{
					values[6] = convert_requires_to_datum(control->requires);
					nulls[6] = false;
				}
				/* comment stays the same */

				tuplestore_putvalues(tupstore, tupdesc, values, nulls);
			}
		}
	}
}

/*
 * Given an initial and final version name, identify the sequence of update
 * scripts that have to be applied to perform that update.
 *
 * Result is a List of names of versions to transition through (the initial
 * version is *not* included).
 */
static List *
identify_update_path(ExtensionControlFile *control,
					 const char *oldVersion, const char *newVersion)
{
	List	   *result;
	List	   *evi_list;
	ExtensionVersionInfo *evi_start;
	ExtensionVersionInfo *evi_target;

	/* Extract the version update graph from the script directory */
	evi_list = get_ext_ver_list(control);

	/* Initialize start and end vertices */
	evi_start = get_ext_ver_info(oldVersion, &evi_list);
	evi_target = get_ext_ver_info(newVersion, &evi_list);

	/* Find shortest path */
	result = find_update_path(evi_list, evi_start, evi_target, false, false);

	if (result == NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("extension \"%s\" has no update path from version \"%s\" to version \"%s\"",
						control->name, oldVersion, newVersion)));

	return result;
}

/*
 * Read the whole of file into memory.
 *
 * The file contents are returned as a single palloc'd chunk. For convenience
 * of the callers, an extra \0 byte is added to the end.
 */
static char *
read_whole_file(const char *filename, int *length)
{
	char	   *buf;
	FILE	   *file;
	size_t		bytes_to_read;
	struct stat fst;

	if (stat(filename, &fst) < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not stat file \"%s\": %m", filename)));

	if (fst.st_size > (MaxAllocSize - 1))
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("file \"%s\" is too large", filename)));
	bytes_to_read = (size_t) fst.st_size;

	if ((file = AllocateFile(filename, PG_BINARY_R)) == NULL)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\" for reading: %m",
						filename)));

	buf = (char *) palloc(bytes_to_read + 1);

	*length = fread(buf, 1, bytes_to_read, file);

	if (ferror(file))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read file \"%s\": %m", filename)));

	FreeFile(file);

	buf[*length] = '\0';
	return buf;
}

static void
InitMaterializedSRF(FunctionCallInfo fcinfo, bits32 flags)
{
	bool		random_access;
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	Tuplestorestate *tupstore;
	MemoryContext old_context,
				per_query_ctx;
	TupleDesc	stored_tupdesc;

	/* check to see if caller supports returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize) ||
		((flags & MAT_SRF_USE_EXPECTED_DESC) != 0 && rsinfo->expectedDesc == NULL))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not allowed in this context")));

	/*
	 * Store the tuplestore and the tuple descriptor in ReturnSetInfo.  This
	 * must be done in the per-query memory context.
	 */
	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	old_context = MemoryContextSwitchTo(per_query_ctx);

	/* build a tuple descriptor for our result type */
	if ((flags & MAT_SRF_USE_EXPECTED_DESC) != 0)
		stored_tupdesc = CreateTupleDescCopy(rsinfo->expectedDesc);
	else
	{
		if (get_call_result_type(fcinfo, NULL, &stored_tupdesc) != TYPEFUNC_COMPOSITE)
			elog(ERROR, "return type must be a row type");
	}

	/* If requested, bless the tuple descriptor */
	if ((flags & MAT_SRF_BLESS) != 0)
		BlessTupleDesc(stored_tupdesc);

	random_access = (rsinfo->allowedModes & SFRM_Materialize_Random) != 0;

	tupstore = tuplestore_begin_heap(random_access, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = stored_tupdesc;
	MemoryContextSwitchTo(old_context);
}

/*
 * Locate the nearest unprocessed ExtensionVersionInfo
 *
 * This part of the algorithm is also about O(N^2).  A priority queue would
 * make it much faster, but for now there's no need.
 */
static ExtensionVersionInfo *
get_nearest_unprocessed_vertex(List *evi_list)
{
	ExtensionVersionInfo *evi = NULL;
	ListCell   *lc;

	foreach(lc, evi_list)
	{
		ExtensionVersionInfo *evi2 = (ExtensionVersionInfo *) lfirst(lc);

		/* only vertices whose distance is still uncertain are candidates */
		if (evi2->distance_known)
			continue;
		/* remember the closest such vertex */
		if (evi == NULL ||
			evi->distance > evi2->distance)
			evi = evi2;
	}

	return evi;
}

/*
 * get_extension_schema - given an extension OID, fetch its extnamespace
 *
 * Returns InvalidOid if no such extension.
 *
 * Note: e20b1ea157 makes this an external function, so we do not need
 * to define this for newer version of PostgreSQL.
 */
static Oid
get_extension_schema(Oid ext_oid)
{
	Oid			result;
	Relation	rel;
	SysScanDesc scandesc;
	HeapTuple	tuple;
	ScanKeyData entry[1];

	rel = table_open(ExtensionRelationId, AccessShareLock);

	ScanKeyInit(&entry[0],
				Anum_pg_extension_oid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(ext_oid));

	scandesc = systable_beginscan(rel, ExtensionOidIndexId, true,
								  NULL, 1, entry);

	tuple = systable_getnext(scandesc);

	/* We assume that there can be at most one matching tuple */
	if (HeapTupleIsValid(tuple))
		result = ((Form_pg_extension) GETSTRUCT(tuple))->extnamespace;
	else
		result = InvalidOid;

	systable_endscan(scandesc);

	table_close(rel, AccessShareLock);

	return result;
}

void
tleRemoveExtensionById(Oid extId)
{
	Relation	rel;
	SysScanDesc scandesc;
	HeapTuple	tuple;
	ScanKeyData entry[1];

	/*
	 * Disallow deletion of any extension that's currently open for insertion;
	 * else subsequent executions of recordDependencyOnCurrentExtension()
	 * could create dangling pg_depend records that refer to a no-longer-valid
	 * pg_extension OID.  This is needed not so much because we think people
	 * might write "DROP EXTENSION foo" in foo's own script files, as because
	 * errors in dependency management in extension script files could give
	 * rise to cases where an extension is dropped as a result of recursing
	 * from some contained object.  Because of that, we must test for the case
	 * here, not at some higher level of the DROP EXTENSION command.
	 */
	if (extId == CurrentExtensionObject)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot drop extension \"%s\" because it is being modified",
						get_extension_name(extId))));

	rel = table_open(ExtensionRelationId, RowExclusiveLock);

	ScanKeyInit(&entry[0],
				Anum_pg_extension_oid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(extId));
	scandesc = systable_beginscan(rel, ExtensionOidIndexId, true,
								  NULL, 1, entry);

	tuple = systable_getnext(scandesc);

	/* We assume that there can be at most one matching tuple */
	if (HeapTupleIsValid(tuple))
		CatalogTupleDelete(rel, &tuple->t_self);

	systable_endscan(scandesc);

	table_close(rel, RowExclusiveLock);
}