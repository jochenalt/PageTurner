
#include <model.hpp>

void ModelConfigDataType::println(const char* format, ...) {
    char s[256];
 	  __gnuc_va_list  args;		
    va_start(args, format);
    vsprintf (s, format, args);
    va_end(args);
    LOGSerial.println(s);
};

void ModelConfigDataType::print() {

    if (modelIsPresent)  {
        println("model %i is present", modelVersion);
    }
    else {
        println("no model present");
    }        
}

void ModelConfigDataType::setup() {
	modelIsPresent = false;
  	modelVersion = 0;
}
