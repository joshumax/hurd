/* This file is used by the Makefile rules for generating
   Xioctl-proto.defs, see Makefile for details.  */

#define CMD(request)	_IOC_COMMAND (request)
#define TYPE(request)	_IOC_TYPE (request)
#define INOUT(request)	_IOC_INOUT (request)

#define SUBID(request)	IOC_COMMAND_SUBID (_IOC_COMMAND (request))

#define TYPE0(request)	_IOT_TYPE0 (_IOC_TYPE (request))
#define TYPE1(request)	_IOT_TYPE1 (_IOC_TYPE (request))
#define TYPE2(request)	_IOT_TYPE2 (_IOC_TYPE (request))
#define COUNT0(request)	_IOT_COUNT0 (_IOC_TYPE (request))
#define COUNT1(request)	_IOT_COUNT1 (_IOC_TYPE (request))
#define COUNT2(request)	_IOT_COUNT2 (_IOC_TYPE (request))
