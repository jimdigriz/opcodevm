#define XSTR(x) STR(x)
#define STR(x) #x

#define XCONCAT(a,b) CONCAT(a,b)
#define CONCAT(a,b) a##b

#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))
