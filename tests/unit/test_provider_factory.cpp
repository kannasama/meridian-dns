#include "providers/ProviderFactory.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "common/Errors.hpp"
#include "providers/PowerDnsProvider.hpp"

using dns::providers::ProviderFactory;

TEST(ProviderFactoryTest, CreatesPowerDnsProvider) {
  auto upProvider = ProviderFactory::create("powerdns", "http://localhost:8081", "test-key");
  ASSERT_NE(upProvider, nullptr);
  EXPECT_EQ(upProvider->name(), "powerdns");
}

TEST(ProviderFactoryTest, UnknownTypeThrows) {
  EXPECT_THROW(ProviderFactory::create("unknown", "http://localhost", "key"),
               dns::common::ValidationError);
}

TEST(ProviderFactoryTest, CloudflareNotYetImplemented) {
  EXPECT_THROW(ProviderFactory::create("cloudflare", "https://api.cloudflare.com", "key"),
               dns::common::ValidationError);
}

TEST(ProviderFactoryTest, DigitalOceanNotYetImplemented) {
  EXPECT_THROW(ProviderFactory::create("digitalocean", "https://api.digitalocean.com", "key"),
               dns::common::ValidationError);
}
