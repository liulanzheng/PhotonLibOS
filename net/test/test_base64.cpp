/*
Copyright 2022 The Photon Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <photon/net/utils.h>

struct Base64TestData {
		std::string  str;
		std::string  base64str;
};
static Base64TestData t[]={
		{"123456789900abcdedf", "MTIzNDU2Nzg5OTAwYWJjZGVkZg=="},
		{"WARRANTIESORCONDITIONSOFANYKINDeitherexpress", "V0FSUkFOVElFU09SQ09ORElUSU9OU09GQU5ZS0lORGVpdGhlcmV4cHJlc3M="},
		{"gogo1233sjjjjasdadjjjzxASDF", "Z29nbzEyMzNzampqamFzZGFkampqenhBU0RG"}
};

TEST(Net, Base64Encode) {
	for (size_t i=0;i<sizeof(t)/sizeof(t[0]); i++ ) {
		std::string ret;
		photon::net::Base64Encode(t[i].str, ret);
	    EXPECT_EQ(ret, t[i].base64str);
	}
}
TEST(Net, Base64Decode) {
	for (size_t i=0;i<sizeof(t)/sizeof(t[0]); i++ ) {
		std::string ret;
		photon::net::Base64Decode(t[i].base64str, ret);
	    EXPECT_EQ(ret, t[i].str);
	}
}
