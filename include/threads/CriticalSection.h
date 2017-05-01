/*
 * CriticalSection.h
 *
 *  Created on: Jan 2, 2017
 *      Author: jeffrey
 */

#pragma once
#include <threads/Lockables.h>
#include <threads/Mutex.h>

class CCriticalSection: public vmodule::CountingLockable<vmodule::CMutex> {};
//typedef vmodule::CountingLockable<vmodule::CMutex> CCriticalSection;

