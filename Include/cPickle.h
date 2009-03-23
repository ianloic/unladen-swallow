#ifndef Py_CPICKLE_H
#define Py_CPICKLE_H
#ifdef __cplusplus
extern "C" {
#endif

/* Header file for cPickle.c. This is here so _testcapimodule.c can test the
   PyMemoTable implementation. */

/* Do this dance so _testcapimodule.c will build correctly, but cPickle will
   be as fast as possible. */
#ifdef NO_STATIC_MEMOTABLE
#define STATIC_MEMOTABLE
#else
#define STATIC_MEMOTABLE static
#endif

typedef struct {
	void *me_key;
	long me_value;
} PyMemoEntry;

typedef struct {
	Py_ssize_t mt_mask;
	Py_ssize_t mt_used;
	Py_ssize_t mt_allocated;
	PyMemoEntry *mt_table;
} PyMemoTable;


/* Create a new PyMemoTable struct, properly initialized.
   Returns NULL on failure. */
STATIC_MEMOTABLE PyMemoTable *PyMemoTable_New(void);

/* Free a PyMemoTable struct, taking care of any internal frees. */
STATIC_MEMOTABLE void PyMemoTable_Del(PyMemoTable *self);

/* Return the number of items stored in the memo. */
STATIC_MEMOTABLE Py_ssize_t PyMemoTable_Size(PyMemoTable *self);

/* Delete all entries in the memo. Returns 0 on success, -1 on failure. */
STATIC_MEMOTABLE int PyMemoTable_Clear(PyMemoTable *self);

/* Return a pointer to the value keyed to `key`. Returns NULL if `key` isn't
   in the memo. */
STATIC_MEMOTABLE long *PyMemoTable_Get(PyMemoTable *self, void *key);

/* Add a new key/value pair to the memo. Returns 0 on success, -1 on failure. */
STATIC_MEMOTABLE int PyMemoTable_Set(PyMemoTable *self, void *key, long value);

/* Copy an existing PyMemoTable struct. Returns NULL on failure. */
STATIC_MEMOTABLE PyMemoTable *PyMemoTable_Copy(PyMemoTable *self);

#ifdef __cplusplus
}
#endif
#endif /* !Py_CPICKLE_H */
