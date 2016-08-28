#define XSTR(x) STR(x)
#define STR(x) #x

#define XCONCAT(a,b) CONCAT(a,b)
#define CONCAT(a,b) a##b

#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

// slight twist on /usr/include/unistd.h:TEMP_FAILURE_RETRY()
#define EINTRSAFE(s, ...)	(__extension__ ({					\
					long int __result;				\
					do {						\
						__result = s(__VA_ARGS__);		\
						if (__result != -1)			\
							break;				\
						else {					\
							if (errno == EINTR)		\
								continue;		\
							err(EX_OSERR, "%s()", #s);	\
						}					\
					} while (1);					\
					__result;					\
				}))
