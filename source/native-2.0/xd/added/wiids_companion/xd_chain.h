#pragma once
#include "xd_companion.h"
// Owned intents advance only at a verified native menu/input boundary.
extern "C" {
void WiidsXD_ChainBegin(int action,int argument,uint32_t actor,int sourceMenu=0);
void WiidsXD_ChainCancel(void);
void WiidsXD_SetChainTrace(void(*sink)(const char*,int,int,int));
int WiidsXD_ChainStep(const XdSnapshot* frame,int physical,XdRequest* request);
}
