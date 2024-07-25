bool
parse_bool(const char *value, bool *result)
{
	return parse_bool_with_len(value, strlen(value), result);
}

bool
parse_bool_with_len(const char *value, size_t len, bool *result)
{
	switch (*value)
	{
		case 't':
		case 'T':
			if (pg_strncasecmp(value, "true", len) == 0)
			{
				if (result)
					*result = true;
				return true;
			}
			break;
		case 'f':
		case 'F':
			if (pg_strncasecmp(value, "false", len) == 0)
			{
				if (result)
					*result = false;
				return true;
			}
			break;
		case 'y':
		case 'Y':
			if (pg_strncasecmp(value, "yes", len) == 0)
			{
				if (result)
					*result = true;
				return true;
			}
			break;
		case 'n':
		case 'N':
			if (pg_strncasecmp(value, "no", len) == 0)
			{
				if (result)
					*result = false;
				return true;
			}
			break;
		case 'o':
		case 'O':
			/* 'o' is not unique enough */
			if (pg_strncasecmp(value, "on", (len > 2 ? len : 2)) == 0)
			{
				if (result)
					*result = true;
				return true;
			}
			else if (pg_strncasecmp(value, "off", (len > 2 ? len : 2)) == 0)
			{
				if (result)
					*result = false;
				return true;
			}
			break;
		case '1':
			if (len == 1)
			{
				if (result)
					*result = true;
				return true;
			}
			break;
		case '0':
			if (len == 1)
			{
				if (result)
					*result = false;
				return true;
			}
			break;
		default:
			break;
	}

	if (result)
		*result = false;		/* suppress compiler warning */
	return false;
}