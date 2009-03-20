#include "rp_object.h"
#include "stdio.h"

RUBY_EXTERN VALUE mRubyPythonBridge;
RUBY_EXTERN VALUE ePythonError;

VALUE cBlankObject;

// :nodoc:
VALUE blank_undef_if(VALUE name,VALUE klass)
{
	VALUE mname=rb_funcall(name,rb_intern("to_s"),0);
	if(rb_funcall(mname,rb_intern("match"),1,rb_str_new2("(?:^__)|(?:\\?$)|(?:^send$)|(?:^class$)"))==Qnil)
	{
		rb_undef_method(klass,STR2CSTR(mname));
		return Qtrue;
	}
	else
	{
		return Qfalse;
	}
}

// :nodoc:
VALUE blank_obj_prep(VALUE self)
{
	VALUE instance_methods=rb_funcall(self,rb_intern("instance_methods"),0);
	rb_iterate(rb_each,instance_methods,blank_undef_if,self);
	return self;
}

VALUE cRubyPyObject;
VALUE cRubyPyModule;
VALUE cRubyPyClass;
VALUE cRubyPyFunction;
VALUE cRubyPyInstance;

void rp_obj_mark(PObj* self)
{}

void rp_obj_free(PObj* self)
{
	if(Py_IsInitialized()&&self->pObject)
	{
		Py_XDECREF(self->pObject);
	}
	free(self);
}


/*
Decreases the reference count on the object wrapped by this instance.
This is used for cleanup in RubyPython.stop. RubyPyObject instances automatically
decrease the reference count on their associated objects before they are garbage collected.
*/
VALUE rp_obj_free_pobj(VALUE self)
{
	PObj *cself;
	Data_Get_Struct(self,PObj,cself);
	if(Py_IsInitialized()&&cself->pObject)
	{
		Py_XDECREF(cself->pObject);
		cself->pObject=NULL;
		return Qtrue;
	}
	else
	{
		cself->pObject=NULL;
	}
	return Qfalse;
}

VALUE rp_obj_alloc(VALUE klass)
{
	PObj* self=ALLOC(PObj);
	self->pObject=NULL;
	return Data_Wrap_Struct(klass,rp_obj_mark,rp_obj_free,self);
}


PyObject* rp_obj_pobject(VALUE self)
{
	PObj *cself;
	Data_Get_Struct(self,PObj,cself);
	if(!cself->pObject)
	{
		rb_raise(ePythonError,"RubyPython tried to access a freed object");
	}
	return cself->pObject;
}

/*
Returns the name of the Python object which this instance wraps.

*/
VALUE rp_obj_name(VALUE self)
{
	if(Py_IsInitialized())
	{
		PyObject *pObject,*pName,*pRepr;
		VALUE rName;
		pObject=rp_obj_pobject(self);
		pName=PyObject_GetAttrString(pObject,"__name__");
		if(!pName)
		{
			PyErr_Clear();
			pName=PyObject_GetAttrString(pObject,"__class__");
	 		pRepr=PyObject_Repr(pName);
			rName=ptor_string(pRepr);
			Py_XDECREF(pRepr);
			return rb_str_concat(rb_str_new2("An instance of "),rName);
			if(!pName)
			{
				PyErr_Clear();
				pName=PyObject_Repr(pObject);
				if(!pName)
				{
					PyErr_Clear();
					return rb_str_new2("__Unnameable__");
				}
			}
		}
		rName=ptor_string(pName);
		Py_XDECREF(pName);
		return rName;
	}
	return rb_str_new2("__FREED__");

}

VALUE rp_obj_from_pyobject(PyObject *pObj)
{
	PObj* self;
	VALUE rObj=rb_class_new_instance(0,NULL,cRubyPyObject);
	Data_Get_Struct(rObj,PObj,self);
	self->pObject=pObj;
	return rObj;
}

VALUE rp_inst_from_instance(PyObject *pInst)
{
	PObj* self;
	VALUE rInst=rb_class_new_instance(0,NULL,cRubyPyInstance);
	PyObject *pClassDict,*pClass,*pInstDict;
	VALUE rInstDict,rClassDict;
	Data_Get_Struct(rInst,PObj,self);
	self->pObject=pInst;
	pClass=PyObject_GetAttrString(pInst,"__class__");
	pClassDict=PyObject_GetAttrString(pClass,"__dict__");
	pInstDict=PyObject_GetAttrString(pInst,"__dict__");
	Py_XINCREF(pClassDict);
	Py_XINCREF(pInstDict);
	rClassDict=rp_obj_from_pyobject(pClassDict);
	rInstDict=rp_obj_from_pyobject(pInstDict);
	rb_iv_set(rInst,"@pclassdict",rClassDict);
	rb_iv_set(rInst,"@pinstdict",rInstDict);
	return rInst;
}

VALUE rp_inst_attr_set(VALUE self,VALUE args)
{
	VALUE name,name_string,rClassDict,result,rInstDict;
	VALUE ret;
	int instance;
	char *cname;
	PObj *pClassDict,*pInstDict,*pDict;
	PyObject *pName;
	name=rb_ary_shift(args);
	name_string=rb_funcall(name,rb_intern("to_s"),0);
	rb_funcall(name_string,rb_intern("chop!"),0);	
	if(!rp_has_attr(self,name_string))
	{		
		int argc;		
		VALUE *argv;
		argc=RARRAY_LEN(args);
		argv=ALLOC_N(VALUE,argc);
		MEMCPY(argv,RARRAY_PTR(args),VALUE,argc);
		return rb_call_super(argc,argv);
	}
	
	cname=STR2CSTR(name_string);
	
	if((NUM2INT(rb_funcall(args,rb_intern("size"),0))==1))
	{
		args=rb_ary_entry(args,0);
	}
	
		
	rClassDict=rb_iv_get(self,"@pclassdict");
	rInstDict=rb_iv_get(self,"@pinstdict");
	
	Data_Get_Struct(rClassDict,PObj,pClassDict);
	Data_Get_Struct(rInstDict,PObj,pInstDict);
	pName=PyString_FromString(cname);
	if(PyDict_Contains(pInstDict->pObject,pName))
	{
		pDict=pInstDict;

	}
	else
	{
		pDict=pClassDict;
		
	}
	Py_XDECREF(pName);
	PyDict_SetItemString(pDict->pObject,STR2CSTR(name_string),rtop_obj(args,0));
	return Qtrue;
}

//:nodoc:
VALUE rp_inst_delegate(VALUE self,VALUE args)
{
	VALUE name,name_string,rClassDict,result,rInstDict;
	VALUE ret;
	char *cname;
	PObj *pClassDict,*pInstDict;
	PyObject *pCalled;
	
	if(rp_equal(args))
	{
		return rp_inst_attr_set(self,args);
	}
	if(!rp_has_attr(self,rb_ary_entry(args,0)))
	{		
		int argc;
		
		VALUE *argv;
		argc=RARRAY_LEN(args);
		argv=ALLOC_N(VALUE,argc);
		MEMCPY(argv,RARRAY_PTR(args),VALUE,argc);
		return rb_call_super(argc,argv);
	}
	name=rb_ary_shift(args);
	name_string=rb_funcall(name,rb_intern("to_s"),0);
	cname=STR2CSTR(name_string);
		
	rClassDict=rb_iv_get(self,"@pclassdict");
	rInstDict=rb_iv_get(self,"@pinstdict");
	Data_Get_Struct(rClassDict,PObj,pClassDict);
	Data_Get_Struct(rInstDict,PObj,pInstDict);
	pCalled=PyDict_GetItemString(pInstDict->pObject,cname);
	if(!pCalled)
	{
		pCalled=PyDict_GetItemString(pClassDict->pObject,cname);
	}
	Py_XINCREF(pCalled);
	result=ptor_obj_no_destruct(pCalled);
	if(rb_obj_is_instance_of(result,cRubyPyFunction))
	{
		Py_XINCREF(rp_obj_pobject(self));
		rb_ary_unshift(args,self);
		ret=rp_call_func(pCalled,args);
		return ret;
	}
	return result;
	
}


VALUE rp_cla_from_class(PyObject *pClass)
{
	PObj* self;
	VALUE rClass=rb_class_new_instance(0,NULL,cRubyPyClass);
	PyObject *pClassDict;
	VALUE rDict;
	Data_Get_Struct(rClass,PObj,self);
	self->pObject=pClass;
	pClassDict=PyObject_GetAttrString(pClass,"__dict__");
	Py_XINCREF(pClassDict);
	rDict=rp_obj_from_pyobject(pClassDict);
	rb_iv_set(rClass,"@pdict",rDict);
	return rClass;
}

VALUE rp_func_from_function(PyObject *pFunc)
{
	PObj* self;
	VALUE rFunc=rb_class_new_instance(0,NULL,cRubyPyFunction);
	Data_Get_Struct(rFunc,PObj,self);
	self->pObject=pFunc;
	return rFunc;
}

VALUE rp_obj_wrap(PyObject* pObj)
{
	VALUE rObj;
	if(PyFunction_Check(pObj)||PyMethod_Check(pObj)||!PyObject_HasAttrString(pObj,"__dict__"))
	{
		return rp_func_from_function(pObj);

	}
	if(PyInstance_Check(pObj))
	{
		rObj=rp_inst_from_instance(pObj);
		return rObj;
	}
	return rp_cla_from_class(pObj);
}

VALUE rp_mod_call_func(VALUE self,VALUE func_name,VALUE args)
{
	PObj *cself;
	Data_Get_Struct(self,PObj,cself);
	PyObject *pModule,*pFunc;
	VALUE rReturn;
	
	pModule=cself->pObject;
	pFunc=rp_get_func_with_module(pModule,func_name);
	rReturn=rp_call_func(pFunc,args);
	Py_XDECREF(pFunc);
	
	return rReturn;
	
}


int rp_has_attr(VALUE self,VALUE func_name)
{
	
	PObj *cself;
	VALUE rName;
	Data_Get_Struct(self,PObj,cself);
	rName=rb_funcall(func_name,rb_intern("to_s"),0);
	if(PyObject_HasAttrString(cself->pObject,STR2CSTR(rName))) return 1;
	return 0;
}

//:nodoc:
VALUE rp_mod_init(VALUE self, VALUE mname)
{
	PObj* cself;
	Data_Get_Struct(self,PObj,cself);
	cself->pObject=rp_get_module(mname);
	VALUE rDict;
	PyObject *pModuleDict;
	pModuleDict=PyModule_GetDict(cself->pObject);
	Py_XINCREF(pModuleDict);
	rDict=rp_obj_from_pyobject(pModuleDict);
	rb_iv_set(self,"@pdict",rDict);
	return self;
}

//Not completely accurate
int rp_is_func(VALUE pObj)
{
	PObj* self;
	Data_Get_Struct(pObj,PObj,self);
	Py_XINCREF(self->pObject);
	return (PyFunction_Check(self->pObject)||PyMethod_Check(self->pObject));
}

VALUE rp_cla_new_inst(VALUE self,VALUE args)
{
	PyObject *pSelf;
	pSelf=rp_obj_pobject(self);
	return rp_call_func(pSelf,args);
}

VALUE rp_obj_responds(VALUE self,VALUE mname)
{
	if(rp_has_attr(self,mname))
	{
		return Qtrue;
	}
	return Qfalse;
}

int rp_equal(VALUE args)
{
	VALUE mname=rb_ary_entry(args,0);
	VALUE name_string=rb_funcall(mname,rb_intern("to_s"),0);
	return Qtrue==rb_funcall(name_string,rb_intern("end_with?"),1,rb_str_new2("="));
}



int rp_double_bang(VALUE args)
{
	VALUE mname=rb_ary_entry(args,0);
	VALUE name_string=rb_funcall(mname,rb_intern("to_s"),0);
	return Qtrue==rb_funcall(name_string,rb_intern("end_with?"),1,rb_str_new2("!!"));
}

VALUE rp_mod_attr_set(VALUE self,VALUE args)
{
	VALUE rDict;
	PObj *pDict;
	VALUE mname=rb_ary_shift(args);
	VALUE name_string=rb_funcall(mname,rb_intern("to_s"),0);
	rb_funcall(name_string,rb_intern("chop!"),0);
	if(!rp_has_attr(self,name_string))
	{		
		int argc;
		
		VALUE *argv;
		argc=RARRAY_LEN(args);
		argv=ALLOC_N(VALUE,argc);
		MEMCPY(argv,RARRAY_PTR(args),VALUE,argc);
		return rb_call_super(argc,argv);
	}
	if(NUM2INT(rb_funcall(args,rb_intern("size"),0))==1)
	{
		args=rb_ary_entry(args,0);
	}
	rDict=rb_iv_get(self,"@pdict");
	Data_Get_Struct(rDict,PObj,pDict);
	PyDict_SetItemString(pDict->pObject,STR2CSTR(name_string),rtop_obj(args,0));
	return Qtrue;
}

//:nodoc:
VALUE rp_mod_delegate(VALUE self,VALUE args)
{
	VALUE name,name_string,rDict,result;
	VALUE ret;
	PObj *pDict;
	PyObject *pCalled;
	if(rp_equal(args))
	{
		return rp_mod_attr_set(self,args);
	}
	// if(rp_double_bang)
	// {
	// 	return rp_mod_attr_db(args);
	// }
	if(!rp_has_attr(self,rb_ary_entry(args,0)))
	{		
		int argc;
		
		VALUE *argv;
		argc=RARRAY_LEN(args);
		argv=ALLOC_N(VALUE,argc);
		MEMCPY(argv,RARRAY_PTR(args),VALUE,argc);
		return rb_call_super(argc,argv);
	}
	name=rb_ary_shift(args);
	name_string=rb_funcall(name,rb_intern("to_s"),0);
		
	rDict=rb_iv_get(self,"@pdict");
	Data_Get_Struct(rDict,PObj,pDict);
	pCalled=PyDict_GetItemString(pDict->pObject,STR2CSTR(name_string));
	Py_XINCREF(pCalled);
	result=ptor_obj_no_destruct(pCalled);
	if(rb_obj_is_instance_of(result,cRubyPyFunction))
	{
		ret=rp_call_func(pCalled,args);
		return ret;
	}
	else if(rb_obj_is_instance_of(result,cRubyPyClass)&&(rb_funcall(args,rb_intern("empty?"),0)==Qfalse)&&PyCallable_Check(pCalled))
	{
		ret=rp_call_func(pCalled,args);
		return ret;
	}
	return result;
	
}

// :nodoc:
inline void Init_BlankObject()
{
	cBlankObject=rb_define_class_under(mRubyPythonBridge,"BlankObject",rb_cObject);
	blank_obj_prep(cBlankObject);
}

/*
A wrapper class for Python objects that allows them to manipulated from within ruby.

Important wrapper functionality is found in the RubyPyModule, RubyPyClass, and RubyPyFunction
classes which wrap Python objects of similar names.

*/
inline void Init_RubyPyObject()
{
	cRubyPyObject=rb_define_class_under(mRubyPythonBridge,"RubyPyObject",cBlankObject);
	rb_define_alloc_func(cRubyPyObject,rp_obj_alloc);
	rb_define_method(cRubyPyObject,"free_pobj",rp_obj_free_pobj,0);
	rb_define_method(cRubyPyObject,"__name",rp_obj_name,0);
	rb_define_method(cRubyPyObject,"respond_to?",rp_obj_responds,1);
	
}


/*
A wrapper class for Python Modules.

Methods calls are delegated to the equivalent Python methods/functions. Attribute references
return either the equivalent attribute converted to a native Ruby type, or wrapped reference 
to a Python object. RubyPyModule instances should be created through the use of RubyPython.import.

*/
void Init_RubyPyModule()
{
	cRubyPyModule=rb_define_class_under(mRubyPythonBridge,"RubyPyModule",cRubyPyObject);
	rb_define_method(cRubyPyModule,"initialize",rp_mod_init,1);
	rb_define_method(cRubyPyModule,"method_missing",rp_mod_delegate,-2);
}

/*
A wrapper class for Python classes and instances.

This allows objects which cannot easily be converted to native Ruby types to still be accessible
from within ruby. Most users need not concern themselves with anything about this class except
its existence.

*/
void Init_RubyPyClass()
{
	cRubyPyClass=rb_define_class_under(mRubyPythonBridge,"RubyPyClass",cRubyPyObject);
	rb_define_method(cRubyPyClass,"method_missing",rp_mod_delegate,-2);
	rb_define_method(cRubyPyClass,"new",rp_cla_new_inst,-2);
}

// 
// A wrapper class for Python functions and methods.
// 
// This is used internally to aid RubyPyClass in delegating method calls.
// 

void Init_RubyPyFunction()
{
	cRubyPyFunction=rb_define_class_under(mRubyPythonBridge,"RubyPyFunction",cRubyPyObject);
}

void Init_RubyPyInstance()
{
	cRubyPyInstance=rb_define_class_under(mRubyPythonBridge,"RubyPyInstance",cRubyPyObject);
	rb_define_method(cRubyPyInstance,"method_missing",rp_inst_delegate,-2);
}
