struct nls_unicode {
	unsigned char uni1;
	unsigned char uni2;
};

struct nls_table {
	char *charset;
	unsigned char **page_uni2charset;
	struct nls_unicode *charset2uni;

	void (*inc_use_count) (void);
	void (*dec_use_count) (void);
	struct nls_table *next;
};

/* nls.c */
extern int init_nls(void);
extern int register_nls(struct nls_table *);
extern int unregister_nls(struct nls_table *);
extern struct nls_table *find_nls(char *);
extern struct nls_table *load_nls(char *);
extern void unload_nls(struct nls_table *);
extern struct nls_table *load_nls_default(void);

extern int utf8_mbtowc(__u16 *, const __u8 *, int);
extern int utf8_mbstowcs(__u16 *, const __u8 *, int);
extern int utf8_wctomb(__u8 *, __u16, int);
extern int utf8_wcstombs(__u8 *, const __u16 *, int);

extern int init_nls_iso8859_1(void);
extern int init_nls_iso8859_2(void);
extern int init_nls_iso8859_3(void);
extern int init_nls_iso8859_4(void);
extern int init_nls_iso8859_5(void);
extern int init_nls_iso8859_6(void);
extern int init_nls_iso8859_7(void);
extern int init_nls_iso8859_8(void);
extern int init_nls_iso8859_9(void);
extern int init_nls_iso8859_15(void);
extern int init_nls_cp437(void);
extern int init_nls_cp737(void);
extern int init_nls_cp775(void);
extern int init_nls_cp850(void);
extern int init_nls_cp852(void);
extern int init_nls_cp855(void);
extern int init_nls_cp857(void);
extern int init_nls_cp860(void);
extern int init_nls_cp861(void);
extern int init_nls_cp862(void);
extern int init_nls_cp863(void);
extern int init_nls_cp864(void);
extern int init_nls_cp865(void);
extern int init_nls_cp866(void);
extern int init_nls_cp869(void);
extern int init_nls_cp874(void);
extern int init_nls_koi8_r(void);
