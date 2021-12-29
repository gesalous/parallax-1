// Copyright [2021] [FORTH-ICS]
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _DJB2_H
#define _DJB2_H

#include <stdint.h>

#ifdef CHECKSUM_DATA_MESSAGES
unsigned long djb2_hash_commulative(unsigned char *, uint32_t, unsigned long);
#endif

unsigned long djb2_hash(unsigned char *, uint32_t);

#endif //_DJB2_H
