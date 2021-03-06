// Copyright 2013-2014 Google Inc. All rights reserved.
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
//
// PKCS#11 s11.11: Signing and MACing functions
//   C_SignInit
//   C_Sign
//   C_SignUpdate
//   C_SignFinal
//   C_SignRecoverInit
//   C_SignRecover
// PKCS#11 s11.12: Functions for verifying signatures and MACs
//   C_VerifyInit
//   C_Verify
//   C_VerifyUpdate
//   C_VerifyFinal
//   C_VerifyRecoverInit
//   C_VerifyRecover
#include "pkcs11test.h"
#include "sha512.hh"

using namespace std;  // So sue me

namespace pkcs11 {
namespace test {

namespace {

class SignTest : public ROUserSessionTest,
                 public ::testing::WithParamInterface<string> {
 public:
  SignTest()
    : info_(kSignatureInfo[GetParam()]),
      public_attrs_({CKA_VERIFY}),
      private_attrs_({CKA_SIGN}),
      datalen_(std::rand() % info_.max_data),
      data_(randmalloc(datalen_)),
      mechanism_({info_.alg, NULL_PTR, 0}) {
  }
 protected:
  SignatureInfo info_;
  vector<CK_ATTRIBUTE_TYPE> public_attrs_;
  vector<CK_ATTRIBUTE_TYPE> private_attrs_;
  const int datalen_;
  unique_ptr<CK_BYTE, freer> data_;
  CK_MECHANISM mechanism_;
};

class SignTestEC : public ROUserSessionTest,
                   public ::testing::WithParamInterface<string> {
 public:
  SignTestEC()
    : info_(kSignatureInfo["ECDSA"]),
      ec_params_(kEccParams[GetParam()]),
      public_attrs_({CKA_VERIFY}),
      private_attrs_({CKA_SIGN}),
      mechanism_({info_.alg, NULL_PTR, 0}) {
    int rand_len = std::rand() % info_.max_data;
    if (mechanism_.mechanism == CKM_ECDSA) {
      unique_ptr<CK_BYTE, freer> rand_data = randmalloc(rand_len);
      string sha512 = sw::sha512::calculate(rand_data.get(), rand_len);
      datalen_ = 64; /* SHA512 */
      data_ = randmalloc(datalen_);
      char buf[2];
      for (int i = 0; i < datalen_; i++) {
        buf[0] = sha512.c_str()[i * 2];
        buf[1] = sha512.c_str()[i * 2 + 1];
        data_.get()[i] = strtoul(buf, NULL, 16);
      }
    } else {
      datalen_ = rand_len;
      data_ = randmalloc(datalen_);
    }
  }
 protected:
  SignatureInfo info_;
  EccParams ec_params_;
  vector<CK_ATTRIBUTE_TYPE> public_attrs_;
  vector<CK_ATTRIBUTE_TYPE> private_attrs_;
  int datalen_;
  unique_ptr<CK_BYTE, freer> data_;
  CK_MECHANISM mechanism_;
};

}  // namespace

#define SKIP_IF_UNIMPLEMENTED_RV(rv) \
    if ((rv) == CKR_MECHANISM_INVALID) {  \
      stringstream ss; \
      ss << "Digest type " << mechanism_type_name(mechanism_.mechanism) << " not implemented"; \
      TEST_SKIPPED(ss.str()); \
      return; \
    }

#define SKIP_IF_KEYPAIR_INVALID(rv) \
    if (!keypair.valid()) {  \
      stringstream ss; \
      ss << "Unable to generate keypair for mechanism " << mechanism_type_name(mechanism_.mechanism); \
      TEST_SKIPPED(ss.str()); \
      return; \
    }

TEST_P(SignTest, SignVerify) {
  KeyPair keypair(session_, public_attrs_, private_attrs_);
  SKIP_IF_KEYPAIR_INVALID(rv);
  CK_RV rv = g_fns->C_SignInit(session_, &mechanism_, keypair.private_handle());
  SKIP_IF_UNIMPLEMENTED_RV(rv);
  ASSERT_CKR_OK(rv);
  CK_BYTE output[1024];
  CK_ULONG output_len = sizeof(output);
  EXPECT_CKR_OK(g_fns->C_Sign(session_, data_.get(), datalen_, output, &output_len));

  ASSERT_CKR_OK(g_fns->C_VerifyInit(session_, &mechanism_, keypair.public_handle()));
  EXPECT_CKR_OK(g_fns->C_Verify(session_, data_.get(), datalen_, output, output_len));
}

TEST_P(SignTest, SignFailVerifyWrong) {
  KeyPair keypair(session_, public_attrs_, private_attrs_);
  SKIP_IF_KEYPAIR_INVALID(rv);
  CK_RV rv = g_fns->C_SignInit(session_, &mechanism_, keypair.private_handle());
  SKIP_IF_UNIMPLEMENTED_RV(rv);
  ASSERT_CKR_OK(rv);
  CK_BYTE output[1024];
  CK_ULONG output_len = sizeof(output);
  EXPECT_CKR_OK(g_fns->C_Sign(session_, data_.get(), datalen_, output, &output_len));

  // Corrupt one byte of the signature.
  output[0]++;

  ASSERT_CKR_OK(g_fns->C_VerifyInit(session_, &mechanism_, keypair.public_handle()));
  EXPECT_CKR(CKR_SIGNATURE_INVALID,
             g_fns->C_Verify(session_, data_.get(), datalen_, output, output_len));
}

TEST_P(SignTest, SignFailVerifyShort) {
  KeyPair keypair(session_, public_attrs_, private_attrs_);
  SKIP_IF_KEYPAIR_INVALID(rv);
  CK_RV rv = g_fns->C_SignInit(session_, &mechanism_, keypair.private_handle());
  SKIP_IF_UNIMPLEMENTED_RV(rv);
  ASSERT_CKR_OK(rv);
  CK_BYTE output[1024];
  CK_ULONG output_len = sizeof(output);
  EXPECT_CKR_OK(g_fns->C_Sign(session_, data_.get(), datalen_, output, &output_len));

  ASSERT_CKR_OK(g_fns->C_VerifyInit(session_, &mechanism_, keypair.public_handle()));
  EXPECT_CKR(CKR_SIGNATURE_LEN_RANGE,
             g_fns->C_Verify(session_, data_.get(), datalen_, output, 4));
}

TEST_F(ROUserSessionTest, SignVerifyRecover) {
  vector<CK_ATTRIBUTE_TYPE> public_attrs = {CKA_VERIFY_RECOVER, CKA_ENCRYPT};
  vector<CK_ATTRIBUTE_TYPE> private_attrs = {CKA_SIGN_RECOVER, CKA_DECRYPT};
  KeyPair keypair(session_, public_attrs, private_attrs);
  if (!keypair.valid()) {
    TEST_SKIPPED("Unable to generate valid keypairs");
  }
  const int datalen = 64;
  unique_ptr<CK_BYTE, freer> data = randmalloc(datalen);
  CK_MECHANISM mechanism = {CKM_RSA_PKCS, NULL_PTR, 0};

  CK_RV rv = g_fns->C_SignRecoverInit(session_, &mechanism, keypair.private_handle());
  if (rv == CKR_FUNCTION_NOT_SUPPORTED) {
    TEST_SKIPPED("SignRecover not supported");
    return;
  }
  if ((rv) == CKR_MECHANISM_INVALID) {
    stringstream ss;
    ss << "Digest type " << mechanism_type_name(mechanism.mechanism) << " not implemented";
    TEST_SKIPPED(ss.str());
    return;
  }
  ASSERT_CKR_OK(rv);
  CK_BYTE output[2048];
  CK_ULONG output_len = sizeof(output);
  EXPECT_CKR_OK(g_fns->C_SignRecover(session_, data.get(), datalen, output, &output_len));
  if (g_verbose) {
    cout << "SignRecover on " << datalen << " bytes produced " << output_len << " bytes:" << endl;
    cout << "  " << hex_data(output, output_len) << endl;
  }

  CK_BYTE recovered[2048];
  CK_ULONG recovered_len = sizeof(recovered);
  ASSERT_CKR_OK(g_fns->C_VerifyRecoverInit(session_, &mechanism, keypair.public_handle()));
  ASSERT_CKR_OK(g_fns->C_VerifyRecover(session_, output, output_len, recovered, &recovered_len));
  EXPECT_EQ(datalen, recovered_len);
  EXPECT_EQ(0, memcmp(data.get(), recovered, datalen));
}

INSTANTIATE_TEST_CASE_P(Signatures, SignTest,
                        ::testing::Values("RSA",
                                          "MD5-RSA",
                                          "SHA1-RSA",
                                          "SHA256-RSA",
                                          "SHA384-RSA",
                                          "SHA512-RSA"));

TEST_P(SignTestEC, SignVerify) {
  KeyPairEC keypair(session_, ec_params_.der, public_attrs_, private_attrs_);
  SKIP_IF_KEYPAIR_INVALID(rv);
  CK_RV rv = g_fns->C_SignInit(session_, &mechanism_, keypair.private_handle());
  SKIP_IF_UNIMPLEMENTED_RV(rv);
  ASSERT_CKR_OK(rv);
  CK_BYTE output[1024];
  CK_ULONG output_len = sizeof(output);
  EXPECT_CKR_OK(g_fns->C_Sign(session_, data_.get(), datalen_, output, &output_len));

  ASSERT_CKR_OK(g_fns->C_VerifyInit(session_, &mechanism_, keypair.public_handle()));
  EXPECT_CKR_OK(g_fns->C_Verify(session_, data_.get(), datalen_, output, output_len));
}

TEST_P(SignTestEC, SignFailVerifyWrong) {
  KeyPairEC keypair(session_, ec_params_.der, public_attrs_, private_attrs_);
  SKIP_IF_KEYPAIR_INVALID(rv);
  CK_RV rv = g_fns->C_SignInit(session_, &mechanism_, keypair.private_handle());
  SKIP_IF_UNIMPLEMENTED_RV(rv);
  ASSERT_CKR_OK(rv);
  CK_BYTE output[1024];
  CK_ULONG output_len = sizeof(output);
  EXPECT_CKR_OK(g_fns->C_Sign(session_, data_.get(), datalen_, output, &output_len));

  // Corrupt one byte of the signature.
  output[0]++;

  ASSERT_CKR_OK(g_fns->C_VerifyInit(session_, &mechanism_, keypair.public_handle()));
  EXPECT_CKR(CKR_SIGNATURE_INVALID,
             g_fns->C_Verify(session_, data_.get(), datalen_, output, output_len));
}

TEST_P(SignTestEC, SignFailVerifyShort) {
  KeyPairEC keypair(session_, ec_params_.der, public_attrs_, private_attrs_);
  SKIP_IF_KEYPAIR_INVALID(rv);
  CK_RV rv = g_fns->C_SignInit(session_, &mechanism_, keypair.private_handle());
  SKIP_IF_UNIMPLEMENTED_RV(rv);
  ASSERT_CKR_OK(rv);
  CK_BYTE output[1024];
  CK_ULONG output_len = sizeof(output);
  EXPECT_CKR_OK(g_fns->C_Sign(session_, data_.get(), datalen_, output, &output_len));

  ASSERT_CKR_OK(g_fns->C_VerifyInit(session_, &mechanism_, keypair.public_handle()));
  EXPECT_CKR(CKR_SIGNATURE_LEN_RANGE,
             g_fns->C_Verify(session_, data_.get(), datalen_, output, 4));
}

INSTANTIATE_TEST_CASE_P(SignaturesEC, SignTestEC,
                        ::testing::Values("NIST-SECP192R1",
                                          "NIST-SECP224R1",
                                          "NIST-SECP256R1",
                                          "NIST-SECP384R1",
                                          "NIST-SECP521R1"));

}  // namespace test
}  // namespace pkcs11

