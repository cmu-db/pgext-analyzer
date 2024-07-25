/* Stolen from elog.c */
static void
write_console(const char *line, int len)
{
	int rc;

#ifdef WIN32

	/*
	 * Try to convert the message to UTF16 and write it with WriteConsoleW().
	 * Fall back on write() if anything fails.
	 *
	 * In contrast to write_eventlog(), don't skip straight to write() based
	 * on the applicable encodings.  Unlike WriteConsoleW(), write() depends
	 * on the suitability of the console output code page.  Since we put
	 * stderr into binary mode in SubPostmasterMain(), write() skips the
	 * necessary translation anyway.
	 *
	 * WriteConsoleW() will fail if stderr is redirected, so just fall through
	 * to writing unconverted to the logfile in this case.
	 *
	 * Since we palloc the structure required for conversion, also fall
	 * through to writing unconverted if we have not yet set up
	 * CurrentMemoryContext.
	 */
	if (!in_error_recursion_trouble() && !redirection_done && CurrentMemoryContext != NULL) {
		WCHAR *utf16;
		int	   utf16len;

		utf16 = pgwin32_message_to_UTF16(line, len, &utf16len);
		if (utf16 != NULL) {
			HANDLE stdHandle;
			DWORD  written;

			stdHandle = GetStdHandle(STD_ERROR_HANDLE);
			if (WriteConsoleW(stdHandle, utf16, utf16len, &written, NULL)) {
				pfree(utf16);
				return;
			}

			/*
			 * In case WriteConsoleW() failed, fall back to writing the
			 * message unconverted.
			 */
			pfree(utf16);
		}
	}
#else

	/*
	 * Conversion on non-win32 platforms is not implemented yet. It requires
	 * non-throw version of pg_do_encoding_conversion(), that converts
	 * unconvertible characters to '?' without errors.
	 *
	 * XXX: We have a no-throw version now. It doesn't convert to '?' though.
	 */
#endif

	/*
	 * We ignore any error from write() here.  We have no useful way to report
	 * it ... certainly whining on stderr isn't likely to be productive.
	 */
	rc = write(fileno(stderr), line, len);
	(void)rc;
}

