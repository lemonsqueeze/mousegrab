
/* map character to keycode */
typedef struct charcodemap {
  char key;
  int code;
  int shift;
} charcodemap_t;


/* key sequence */
typedef struct
{
    charcodemap_t *keys;
    int		  nkeys;
} mykey_t;
