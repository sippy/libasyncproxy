#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <Python.h>

#include "asyncproxy.h"

#define MODULE_NAME "asyncproxy.AsyncProxy"

typedef struct {
    PyObject *in2out_cb;
    PyObject *out2in_cb;
    PyObject *on_connect_cb;
    PyObject *on_established_cb;
    PyObject *on_disconnect_cb;
} PyAsyncProxyCallbacks;

typedef struct {
    PyObject_HEAD
    void *ap;
    PyAsyncProxyCallbacks *cbs;
} PyAsyncProxy;

typedef struct {
    PyObject_HEAD
    struct transform_res *res;
    size_t max_len;
} PyTransformRes;

static PyTypeObject PyAsyncProxyType;
static PyTypeObject PyAsyncProxy2FDType;
static PyTypeObject PyTransformResType;

static int
PyAsyncProxy_ensure_callbacks(PyAsyncProxy *self)
{
    if (self->cbs != NULL)
        return 0;
    self->cbs = PyMem_Calloc(1, sizeof(*self->cbs));
    if (self->cbs == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    return 0;
}

static int
PyAsyncProxy_check_handle(PyAsyncProxy *self)
{
    if (self->ap == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "AsyncProxy handle is not initialized");
        return -1;
    }
    return 0;
}

static int
PyTransformRes_check(PyTransformRes *self)
{
    if (self->res == NULL) {
        PyErr_SetString(PyExc_ReferenceError, "transform_res is no longer valid");
        return -1;
    }
    return 0;
}

static PyObject *
PyTransformRes_get_contents(PyTransformRes *self, void *closure)
{
    (void)closure;
    if (PyTransformRes_check(self) != 0)
        return NULL;
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
PyTransformRes_get_buf(PyTransformRes *self, void *closure)
{
    (void)closure;
    if (PyTransformRes_check(self) != 0)
        return NULL;
    return PyLong_FromUnsignedLongLong((unsigned long long)(uintptr_t)self->res->buf);
}

static int
PyTransformRes_set_buf(PyTransformRes *self, PyObject *value, void *closure)
{
    unsigned long long ptr;

    (void)closure;
    if (PyTransformRes_check(self) != 0)
        return -1;
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "buf cannot be deleted");
        return -1;
    }
    ptr = PyLong_AsUnsignedLongLong(value);
    if (PyErr_Occurred())
        return -1;
    self->res->buf = (void *)(uintptr_t)ptr;
    return 0;
}

static PyObject *
PyTransformRes_get_len(PyTransformRes *self, void *closure)
{
    (void)closure;
    if (PyTransformRes_check(self) != 0)
        return NULL;
    return PyLong_FromSize_t(self->res->len);
}

static int
PyTransformRes_set_len(PyTransformRes *self, PyObject *value, void *closure)
{
    unsigned long long len;

    (void)closure;
    if (PyTransformRes_check(self) != 0)
        return -1;
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "len cannot be deleted");
        return -1;
    }
    len = PyLong_AsUnsignedLongLong(value);
    if (PyErr_Occurred())
        return -1;
    if (len > self->max_len) {
        PyErr_SetString(PyExc_ValueError, "len exceeds transform buffer capacity");
        return -1;
    }
    self->res->len = (size_t)len;
    return 0;
}

static PyObject *
PyTransformRes_read(PyTransformRes *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":read"))
        return NULL;
    if (PyTransformRes_check(self) != 0)
        return NULL;
    return PyBytes_FromStringAndSize((const char *)self->res->buf, (Py_ssize_t)self->res->len);
}

static PyObject *
PyTransformRes_write(PyTransformRes *self, PyObject *args)
{
    PyObject *obj;
    Py_buffer view;

    if (!PyArg_ParseTuple(args, "O:write", &obj))
        return NULL;
    if (PyTransformRes_check(self) != 0)
        return NULL;
    if (PyObject_GetBuffer(obj, &view, PyBUF_SIMPLE) != 0)
        return NULL;
    if ((size_t)view.len > self->max_len) {
        PyBuffer_Release(&view);
        PyErr_SetString(PyExc_ValueError, "data exceeds transform buffer capacity");
        return NULL;
    }
    memcpy(self->res->buf, view.buf, (size_t)view.len);
    self->res->len = (size_t)view.len;
    PyBuffer_Release(&view);
    Py_RETURN_NONE;
}

static PyGetSetDef PyTransformRes_getset[] = {
    {"contents", (getter)PyTransformRes_get_contents, NULL, NULL, NULL},
    {"buf", (getter)PyTransformRes_get_buf, (setter)PyTransformRes_set_buf, NULL, NULL},
    {"len", (getter)PyTransformRes_get_len, (setter)PyTransformRes_set_len, NULL, NULL},
    {NULL}
};

static PyMethodDef PyTransformRes_methods[] = {
    {"read", (PyCFunction)PyTransformRes_read, METH_VARARGS, NULL},
    {"write", (PyCFunction)PyTransformRes_write, METH_VARARGS, NULL},
    {NULL}
};

static PyTypeObject PyTransformResType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = MODULE_NAME ".transform_res",
    .tp_basicsize = sizeof(PyTransformRes),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = PyTransformRes_methods,
    .tp_getset = PyTransformRes_getset,
};

static PyObject *
PyTransformRes_FromC(struct transform_res *res, size_t max_len)
{
    PyTransformRes *obj;

    obj = PyObject_New(PyTransformRes, &PyTransformResType);
    if (obj == NULL)
        return NULL;
    obj->res = res;
    obj->max_len = max_len;
    return (PyObject *)obj;
}

static void
PyAsyncProxy_data_callback(PyObject *callable, struct transform_res *res, size_t max_len)
{
    PyObject *arg;
    PyObject *rv;

    if (callable == NULL)
        return;

    arg = PyTransformRes_FromC(res, max_len);
    if (arg == NULL) {
        PyErr_Print();
        res->len = 0;
        return;
    }
    rv = PyObject_CallFunctionObjArgs(callable, arg, NULL);
    ((PyTransformRes *)arg)->res = NULL;
    Py_DECREF(arg);
    if (rv == NULL) {
        PyErr_Print();
        res->len = 0;
        return;
    }
    Py_DECREF(rv);
}

static void
PyAsyncProxy_onconnect_callback(struct asyncproxy_cb_args *args)
{
    PyAsyncProxyCallbacks *cbs;
    PyObject *tres;
    PyObject *max_len_obj;
    PyObject *rv;

    assert(args != NULL);
    cbs = (PyAsyncProxyCallbacks *)args->arg;
    assert(cbs != NULL);
    if (cbs->on_connect_cb == NULL)
        return;

    tres = PyTransformRes_FromC(&args->res, args->max_len);
    if (tres == NULL) {
        PyErr_Print();
        args->res.len = 0;
        return;
    }
    max_len_obj = PyLong_FromSize_t(args->max_len);
    if (max_len_obj == NULL) {
        ((PyTransformRes *)tres)->res = NULL;
        Py_DECREF(tres);
        PyErr_Print();
        args->res.len = 0;
        return;
    }
    rv = PyObject_CallFunctionObjArgs(cbs->on_connect_cb, tres, max_len_obj, NULL);
    Py_DECREF(max_len_obj);
    ((PyTransformRes *)tres)->res = NULL;
    Py_DECREF(tres);
    if (rv == NULL) {
        PyErr_Print();
        args->res.len = 0;
        return;
    }
    Py_DECREF(rv);
}

static void
PyAsyncProxy_onestablished_callback(struct asyncproxy_cb_args *args)
{
    PyAsyncProxyCallbacks *cbs;
    PyObject *tres;
    PyObject *max_len_obj;
    PyObject *rv;

    assert(args != NULL);
    cbs = (PyAsyncProxyCallbacks *)args->arg;
    assert(cbs != NULL);
    if (cbs->on_established_cb == NULL)
        return;

    tres = PyTransformRes_FromC(&args->res, args->max_len);
    if (tres == NULL) {
        PyErr_Print();
        args->res.len = 0;
        return;
    }
    max_len_obj = PyLong_FromSize_t(args->max_len);
    if (max_len_obj == NULL) {
        ((PyTransformRes *)tres)->res = NULL;
        Py_DECREF(tres);
        PyErr_Print();
        args->res.len = 0;
        return;
    }
    rv = PyObject_CallFunctionObjArgs(cbs->on_established_cb, tres, max_len_obj, NULL);
    Py_DECREF(max_len_obj);
    ((PyTransformRes *)tres)->res = NULL;
    Py_DECREF(tres);
    if (rv == NULL) {
        PyErr_Print();
        args->res.len = 0;
        return;
    }
    Py_DECREF(rv);
}

static void
PyAsyncProxy_in2out_callback(struct asyncproxy_cb_args *args)
{
    PyAsyncProxyCallbacks *cbs;

    assert(args != NULL);
    cbs = (PyAsyncProxyCallbacks *)args->arg;
    assert(cbs != NULL);
    PyAsyncProxy_data_callback(cbs->in2out_cb, &args->res, args->max_len);
}

static void
PyAsyncProxy_out2in_callback(struct asyncproxy_cb_args *args)
{
    PyAsyncProxyCallbacks *cbs;

    assert(args != NULL);
    cbs = (PyAsyncProxyCallbacks *)args->arg;
    assert(cbs != NULL);
    PyAsyncProxy_data_callback(cbs->out2in_cb, &args->res, args->max_len);
}

static void
PyAsyncProxy_ondisconnect_callback(void *arg)
{
    PyAsyncProxyCallbacks *cbs = (PyAsyncProxyCallbacks *)arg;
    PyObject *rv;

    assert(cbs != NULL);
    if (cbs->on_disconnect_cb == NULL)
        return;
    rv = PyObject_CallFunctionObjArgs(cbs->on_disconnect_cb, NULL);
    if (rv == NULL) {
        PyErr_Print();
        return;
    }
    Py_DECREF(rv);
}

static void
PyAsyncProxy_clear_callbacks(PyAsyncProxyCallbacks *cbs)
{
    if (cbs == NULL)
        return;
    Py_CLEAR(cbs->in2out_cb);
    Py_CLEAR(cbs->out2in_cb);
    Py_CLEAR(cbs->on_connect_cb);
    Py_CLEAR(cbs->on_established_cb);
    Py_CLEAR(cbs->on_disconnect_cb);
}

static void
PyAsyncProxy_dealloc(PyAsyncProxy *self)
{
    if (self->ap != NULL) {
        void *ap = self->ap;
        self->ap = NULL;
        Py_BEGIN_ALLOW_THREADS
        asyncproxy_dtor(ap);
        Py_END_ALLOW_THREADS
    }
    PyAsyncProxy_clear_callbacks(self->cbs);
    PyMem_Free(self->cbs);
    self->cbs = NULL;
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static int
PyAsyncProxy_init(PyAsyncProxy *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"fd", "dest", "portn", "af", "bindto", NULL};
    struct asyncproxy_ctor_args cargs;
    int fd;
    const char *dest;
    int portn;
    int af;
    const char *bindto = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "isii|z:AsyncProxy", kwlist,
      &fd, &dest, &portn, &af, &bindto))
        return -1;

    memset(&cargs, 0, sizeof(cargs));
    cargs.fd = fd;
    cargs.dest_type = AP_DEST_HOST;
    cargs.dest = dest;
    cargs.portn = (unsigned short)portn;
    cargs.af = af;
    cargs.bindto = bindto;

    if (PyAsyncProxy_ensure_callbacks(self) != 0)
        return -1;

    self->ap = asyncproxy_ctor(&cargs);
    if (self->ap == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "asyncproxy_ctor() failed");
        return -1;
    }
    return 0;
}

static int
PyAsyncProxy2FD_init(PyAsyncProxy *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"fd1", "fd2", NULL};
    struct asyncproxy_ctor_args cargs;
    int fd1;
    int fd2;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii:AsyncProxy2FD", kwlist, &fd1, &fd2))
        return -1;

    memset(&cargs, 0, sizeof(cargs));
    cargs.fd = fd1;
    cargs.dest_type = AP_DEST_FD;
    cargs.out_fd = fd2;

    if (PyAsyncProxy_ensure_callbacks(self) != 0)
        return -1;

    self->ap = asyncproxy_ctor(&cargs);
    if (self->ap == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "asyncproxy_ctor() failed");
        return -1;
    }
    return 0;
}

static int
PyAsyncProxy_set_callback(PyObject **slot, PyObject *callable)
{
    if (callable == Py_None)
        callable = NULL;
    if (callable != NULL && !PyCallable_Check(callable)) {
        PyErr_SetString(PyExc_TypeError, "callback must be callable or None");
        return -1;
    }
    Py_XINCREF(callable);
    Py_XDECREF(*slot);
    *slot = callable;
    return 0;
}

typedef enum {
    PYAP_CB_IN2OUT,
    PYAP_CB_OUT2IN,
    PYAP_CB_ONCONNECT,
    PYAP_CB_ONESTABLISHED,
    PYAP_CB_ONDISCONNECT,
} PyAsyncProxyCallbackKind;

static PyObject **
PyAsyncProxy_callback_slot(PyAsyncProxy *self, PyAsyncProxyCallbackKind kind)
{
    switch (kind) {
    case PYAP_CB_IN2OUT:
        return &self->cbs->in2out_cb;
    case PYAP_CB_OUT2IN:
        return &self->cbs->out2in_cb;
    case PYAP_CB_ONCONNECT:
        return &self->cbs->on_connect_cb;
    case PYAP_CB_ONESTABLISHED:
        return &self->cbs->on_established_cb;
    case PYAP_CB_ONDISCONNECT:
        return &self->cbs->on_disconnect_cb;
    }
    return NULL;
}

static int
PyAsyncProxy_apply_callback(PyAsyncProxy *self, PyAsyncProxyCallbackKind kind,
    PyObject *callable)
{
    struct asyncproxy_cb_info cb_info = {0};
    PyObject **slot;

    slot = PyAsyncProxy_callback_slot(self, kind);
    assert(slot != NULL);
    if (PyAsyncProxy_set_callback(slot, callable) != 0)
        return -1;

    if (*slot != NULL) {
        cb_info.cb_arg = self->cbs;
        switch (kind) {
        case PYAP_CB_IN2OUT:
            cb_info.cb.data = PyAsyncProxy_in2out_callback;
            break;
        case PYAP_CB_OUT2IN:
            cb_info.cb.data = PyAsyncProxy_out2in_callback;
            break;
        case PYAP_CB_ONCONNECT:
            cb_info.cb.onconnect = PyAsyncProxy_onconnect_callback;
            break;
        case PYAP_CB_ONESTABLISHED:
            cb_info.cb.onestablished = PyAsyncProxy_onestablished_callback;
            break;
        case PYAP_CB_ONDISCONNECT:
            cb_info.cb.ondisconnect = PyAsyncProxy_ondisconnect_callback;
            break;
        }
    }

    switch (kind) {
    case PYAP_CB_IN2OUT:
        asyncproxy_set_i2o(self->ap, &cb_info);
        break;
    case PYAP_CB_OUT2IN:
        asyncproxy_set_o2i(self->ap, &cb_info);
        break;
    case PYAP_CB_ONCONNECT:
        asyncproxy_set_onconnect(self->ap, &cb_info);
        break;
    case PYAP_CB_ONESTABLISHED:
        asyncproxy_set_onestablished(self->ap, &cb_info);
        break;
    case PYAP_CB_ONDISCONNECT:
        asyncproxy_set_ondisconnect(self->ap, &cb_info);
        break;
    }
    return 0;
}

static int
PyAsyncProxy_apply_optional_attr_callback(PyAsyncProxy *self, const char *name,
    PyAsyncProxyCallbackKind kind)
{
    PyObject **slot;
    PyObject *callable;
    int rval;

    slot = PyAsyncProxy_callback_slot(self, kind);
    assert(slot != NULL);
    if (*slot != NULL)
        return 0;

    callable = PyObject_GetAttrString((PyObject *)self, name);
    if (callable == NULL) {
        if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
            PyErr_Clear();
            return 0;
        }
        return -1;
    }
    if (callable == Py_None) {
        Py_DECREF(callable);
        return 0;
    }
    rval = PyAsyncProxy_apply_callback(self, kind, callable);
    Py_DECREF(callable);
    return rval;
}

static PyObject *
PyAsyncProxy_set_i2o(PyAsyncProxy *self, PyObject *args)
{
    PyObject *callable;

    if (!PyArg_ParseTuple(args, "O:set_i2o", &callable))
        return NULL;
    if (PyAsyncProxy_check_handle(self) != 0)
        return NULL;
    if (PyAsyncProxy_apply_callback(self, PYAP_CB_IN2OUT, callable) != 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
PyAsyncProxy_set_o2i(PyAsyncProxy *self, PyObject *args)
{
    PyObject *callable;

    if (!PyArg_ParseTuple(args, "O:set_o2i", &callable))
        return NULL;
    if (PyAsyncProxy_check_handle(self) != 0)
        return NULL;
    if (PyAsyncProxy_apply_callback(self, PYAP_CB_OUT2IN, callable) != 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
PyAsyncProxy_set_onconnect(PyAsyncProxy *self, PyObject *args)
{
    PyObject *callable;

    if (!PyArg_ParseTuple(args, "O:set_onconnect", &callable))
        return NULL;
    if (PyAsyncProxy_check_handle(self) != 0)
        return NULL;
    if (PyAsyncProxy_apply_callback(self, PYAP_CB_ONCONNECT, callable) != 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
PyAsyncProxy_set_onestablished(PyAsyncProxy *self, PyObject *args)
{
    PyObject *callable;

    if (!PyArg_ParseTuple(args, "O:set_onestablished", &callable))
        return NULL;
    if (PyAsyncProxy_check_handle(self) != 0)
        return NULL;
    if (PyAsyncProxy_apply_callback(self, PYAP_CB_ONESTABLISHED, callable) != 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
PyAsyncProxy_set_ondisconnect(PyAsyncProxy *self, PyObject *args)
{
    PyObject *callable;

    if (!PyArg_ParseTuple(args, "O:set_ondisconnect", &callable))
        return NULL;
    if (PyAsyncProxy_check_handle(self) != 0)
        return NULL;
    if (PyAsyncProxy_apply_callback(self, PYAP_CB_ONDISCONNECT, callable) != 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
PyAsyncProxy_start(PyAsyncProxy *self, PyObject *args)
{
    int rval;

    if (!PyArg_ParseTuple(args, ":start"))
        return NULL;
    if (PyAsyncProxy_check_handle(self) != 0)
        return NULL;
    if (PyAsyncProxy_apply_optional_attr_callback(self, "in2out", PYAP_CB_IN2OUT) != 0)
        return NULL;
    if (PyAsyncProxy_apply_optional_attr_callback(self, "out2in", PYAP_CB_OUT2IN) != 0)
        return NULL;
    if (PyAsyncProxy_apply_optional_attr_callback(self, "on_connect", PYAP_CB_ONCONNECT) != 0)
        return NULL;
    if (PyAsyncProxy_apply_optional_attr_callback(self, "onestablished", PYAP_CB_ONESTABLISHED) != 0)
        return NULL;
    if (PyAsyncProxy_apply_optional_attr_callback(self, "disc_cb", PYAP_CB_ONDISCONNECT) != 0)
        return NULL;

    Py_BEGIN_ALLOW_THREADS
    rval = asyncproxy_start(self->ap);
    Py_END_ALLOW_THREADS
    if (rval != 0) {
        PyErr_SetString(PyExc_RuntimeError, "asyncproxy_start() failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
PyAsyncProxy_isalive(PyAsyncProxy *self, PyObject *args)
{
    int rval;

    if (!PyArg_ParseTuple(args, ":isAlive"))
        return NULL;
    if (PyAsyncProxy_check_handle(self) != 0)
        return NULL;
    rval = asyncproxy_isalive(self->ap);
    if (rval)
        Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

static PyObject *
PyAsyncProxy_join(PyAsyncProxy *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"shutdown", NULL};
    int shutdown = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|p:join", kwlist, &shutdown))
        return NULL;
    if (PyAsyncProxy_check_handle(self) != 0)
        return NULL;

    Py_BEGIN_ALLOW_THREADS
    asyncproxy_join(self->ap, shutdown);
    Py_END_ALLOW_THREADS
    Py_RETURN_NONE;
}

static PyObject *
PyAsyncProxy_describe(PyAsyncProxy *self, PyObject *args)
{
    const char *desc;

    if (!PyArg_ParseTuple(args, ":describe"))
        return NULL;
    if (PyAsyncProxy_check_handle(self) != 0)
        return NULL;
    desc = asyncproxy_describe(self->ap);
    if (desc == NULL)
        Py_RETURN_NONE;
    return PyUnicode_FromString(desc);
}

static PyObject *
PyAsyncProxy_getsockname(PyAsyncProxy *self, PyObject *args)
{
    const char *addr;
    unsigned short portn = 0;

    if (!PyArg_ParseTuple(args, ":getsockname"))
        return NULL;
    if (PyAsyncProxy_check_handle(self) != 0)
        return NULL;
    addr = asyncproxy_getsockname(self->ap, &portn);
    if (addr == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "asyncproxy_getsockname() failed");
        return NULL;
    }
    return Py_BuildValue("sH", addr, portn);
}

static PyMethodDef PyAsyncProxy_methods[] = {
    {"set_i2o", (PyCFunction)PyAsyncProxy_set_i2o, METH_VARARGS, NULL},
    {"set_o2i", (PyCFunction)PyAsyncProxy_set_o2i, METH_VARARGS, NULL},
    {"set_onconnect", (PyCFunction)PyAsyncProxy_set_onconnect, METH_VARARGS, NULL},
    {"set_onestablished", (PyCFunction)PyAsyncProxy_set_onestablished, METH_VARARGS, NULL},
    {"set_ondisconnect", (PyCFunction)PyAsyncProxy_set_ondisconnect, METH_VARARGS, NULL},
    {"start", (PyCFunction)PyAsyncProxy_start, METH_VARARGS, NULL},
    {"isAlive", (PyCFunction)PyAsyncProxy_isalive, METH_VARARGS, NULL},
    {"join", (PyCFunction)PyAsyncProxy_join, METH_VARARGS | METH_KEYWORDS, NULL},
    {"describe", (PyCFunction)PyAsyncProxy_describe, METH_VARARGS, NULL},
    {"getsockname", (PyCFunction)PyAsyncProxy_getsockname, METH_VARARGS, NULL},
    {NULL}
};

static PyTypeObject PyAsyncProxyType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = MODULE_NAME ".AsyncProxy",
    .tp_basicsize = sizeof(PyAsyncProxy),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)PyAsyncProxy_init,
    .tp_dealloc = (destructor)PyAsyncProxy_dealloc,
    .tp_methods = PyAsyncProxy_methods,
};

static PyTypeObject PyAsyncProxy2FDType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = MODULE_NAME ".AsyncProxy2FD",
    .tp_basicsize = sizeof(PyAsyncProxy),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)PyAsyncProxy2FD_init,
    .tp_dealloc = (destructor)PyAsyncProxy_dealloc,
    .tp_methods = PyAsyncProxy_methods,
};

static PyObject *
PyAsyncProxy_setdebug(PyObject *self, PyObject *args)
{
    int level;

    (void)self;
    if (!PyArg_ParseTuple(args, "i:setdebug", &level))
        return NULL;
    asyncproxy_setdebug(level);
    Py_RETURN_NONE;
}

static PyMethodDef AsyncProxy_module_methods[] = {
    {"setdebug", (PyCFunction)PyAsyncProxy_setdebug, METH_VARARGS, NULL},
    {NULL}
};

static struct PyModuleDef AsyncProxy_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = MODULE_NAME,
    .m_doc = "Native Python interface to libasyncproxy.",
    .m_size = -1,
    .m_methods = AsyncProxy_module_methods,
};

PyMODINIT_FUNC
PyInit_AsyncProxy(void)
{
    PyObject *module;

    if (PyType_Ready(&PyTransformResType) < 0)
        return NULL;
    if (PyType_Ready(&PyAsyncProxyType) < 0)
        return NULL;
    if (PyType_Ready(&PyAsyncProxy2FDType) < 0)
        return NULL;

    module = PyModule_Create(&AsyncProxy_module);
    if (module == NULL)
        return NULL;

    Py_INCREF(&PyAsyncProxyType);
    if (PyModule_AddObject(module, "AsyncProxy", (PyObject *)&PyAsyncProxyType) < 0) {
        Py_DECREF(&PyAsyncProxyType);
        Py_DECREF(module);
        return NULL;
    }
    Py_INCREF(&PyAsyncProxy2FDType);
    if (PyModule_AddObject(module, "AsyncProxy2FD", (PyObject *)&PyAsyncProxy2FDType) < 0) {
        Py_DECREF(&PyAsyncProxy2FDType);
        Py_DECREF(module);
        return NULL;
    }
    Py_INCREF(&PyTransformResType);
    if (PyModule_AddObject(module, "transform_res", (PyObject *)&PyTransformResType) < 0) {
        Py_DECREF(&PyTransformResType);
        Py_DECREF(module);
        return NULL;
    }
    PyModule_AddIntConstant(module, "AP_DEST_HOST", AP_DEST_HOST);
    PyModule_AddIntConstant(module, "AP_DEST_FD", AP_DEST_FD);

    return module;
}
