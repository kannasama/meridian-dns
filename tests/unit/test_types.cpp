#include "common/Types.hpp"

#include <gtest/gtest.h>

TEST(DnsRecordTest, DefaultProviderMetaIsNull) {
  dns::common::DnsRecord dr;
  EXPECT_TRUE(dr.jProviderMeta.is_null());
}

TEST(DnsRecordTest, ProviderMetaStoresJson) {
  dns::common::DnsRecord dr;
  dr.jProviderMeta = {{"proxied", true}};
  EXPECT_TRUE(dr.jProviderMeta.value("proxied", false));
}
