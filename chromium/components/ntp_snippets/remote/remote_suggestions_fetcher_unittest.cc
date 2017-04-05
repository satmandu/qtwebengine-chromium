// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_snippets/remote/remote_suggestions_fetcher.h"

#include <deque>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/memory/ptr_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/histogram_tester.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/ntp_snippets/category.h"
#include "components/ntp_snippets/features.h"
#include "components/ntp_snippets/ntp_snippets_constants.h"
#include "components/ntp_snippets/remote/remote_suggestion.h"
#include "components/ntp_snippets/remote/request_params.h"
#include "components/ntp_snippets/remote/test_utils.h"
#include "components/ntp_snippets/user_classifier.h"
#include "components/prefs/testing_pref_service.h"
#include "components/signin/core/browser/fake_profile_oauth2_token_service.h"
#include "components/signin/core/browser/fake_signin_manager.h"
#include "components/variations/entropy_provider.h"
#include "components/variations/variations_params_manager.h"
#include "google_apis/gaia/fake_oauth2_token_service_delegate.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ntp_snippets {

namespace {

using testing::_;
using testing::AllOf;
using testing::ElementsAre;
using testing::Eq;
using testing::Field;
using testing::IsEmpty;
using testing::Not;
using testing::NotNull;
using testing::Pointee;
using testing::PrintToString;
using testing::Return;
using testing::StartsWith;
using testing::WithArg;

const char kAPIKey[] = "fakeAPIkey";
const char kTestChromeReaderUrl[] =
    "https://chromereader-pa.googleapis.com/v1/fetch?key=fakeAPIkey";
const char kTestChromeContentSuggestionsSignedOutUrl[] =
    "https://chromecontentsuggestions-pa.googleapis.com/v1/suggestions/"
    "fetch?key=fakeAPIkey";
const char kTestChromeContentSuggestionsSignedInUrl[] =
    "https://chromecontentsuggestions-pa.googleapis.com/v1/suggestions/fetch";

const char kTestEmail[] = "foo@bar.com";

// Artificial time delay for JSON parsing.
const int64_t kTestJsonParsingLatencyMs = 20;

ACTION_P(MoveArgument1PointeeTo, ptr) {
  *ptr = std::move(*arg1);
}

MATCHER(HasValue, "") {
  return static_cast<bool>(*arg);
}

// TODO(fhorschig): When there are more helpers for the Status class, consider a
// helpers file.
MATCHER_P(HasCode, code, "") {
  return arg.code == code;
}

MATCHER(IsSuccess, "") {
  return arg.IsSuccess();
}

MATCHER(IsEmptyArticleList, "is an empty list of articles") {
  RemoteSuggestionsFetcher::OptionalFetchedCategories& fetched_categories =
      *arg;
  return fetched_categories && fetched_categories->size() == 1 &&
         fetched_categories->begin()->suggestions.empty();
}

MATCHER_P(IsSingleArticle, url, "is a list with the single article %(url)s") {
  RemoteSuggestionsFetcher::OptionalFetchedCategories& fetched_categories =
      *arg;
  if (!fetched_categories) {
    *result_listener << "got empty categories.";
    return false;
  }
  if (fetched_categories->size() != 1) {
    *result_listener << "expected single category.";
    return false;
  }
  auto category = fetched_categories->begin();
  if (category->suggestions.size() != 1) {
    *result_listener << "expected single snippet, got: "
                     << category->suggestions.size();
    return false;
  }
  if (category->suggestions[0]->url().spec() != url) {
    *result_listener << "unexpected url, got: "
                     << category->suggestions[0]->url().spec();
    return false;
  }
  return true;
}

MATCHER(IsCategoryInfoForArticles, "") {
  if (!arg.has_fetch_action()) {
    *result_listener << "missing expected has_fetc_action";
    return false;
  }
  if (arg.has_view_all_action()) {
    *result_listener << "unexpected has_view_all_action";
    return false;
  }
  if (!arg.show_if_empty()) {
    *result_listener << "missing expected show_if_empty";
    return false;
  }
  return true;
}

MATCHER_P(FirstCategoryHasInfo, info_matcher, "") {
  if (!arg->has_value() || arg->value().size() == 0) {
    *result_listener << "No category found.";
  }
  return testing::ExplainMatchResult(info_matcher, arg->value().front().info,
                                     result_listener);
}

class MockSnippetsAvailableCallback {
 public:
  // Workaround for gMock's lack of support for movable arguments.
  void WrappedRun(
      Status status,
      RemoteSuggestionsFetcher::OptionalFetchedCategories fetched_categories) {
    Run(status, &fetched_categories);
  }

  MOCK_METHOD2(Run,
               void(Status status,
                    RemoteSuggestionsFetcher::OptionalFetchedCategories*
                        fetched_categories));
};

// TODO(fhorschig): Transfer this class' functionality to call delegates
// automatically as option to TestURLFetcherFactory where it was just deleted.
// This can be represented as a single member there and would reduce the amount
// of fake implementations from three to two.

// DelegateCallingTestURLFetcherFactory can be used to temporarily inject
// TestURLFetcher instances into a scope.
// Client code can access the last created fetcher to verify expected
// properties. When the factory gets destroyed, all available delegates of still
// valid fetchers will be called.
// This ensures once-bound callbacks (like SnippetsAvailableCallback) will be
// called at some point and are not leaked.
class DelegateCallingTestURLFetcherFactory
    : public net::TestURLFetcherFactory,
      public net::TestURLFetcherDelegateForTests {
 public:
  DelegateCallingTestURLFetcherFactory() {
    SetDelegateForTests(this);
    set_remove_fetcher_on_delete(true);
  }

  ~DelegateCallingTestURLFetcherFactory() override {
    while (!fetchers_.empty()) {
      DropAndCallDelegate(fetchers_.front());
    }
  }

  std::unique_ptr<net::URLFetcher> CreateURLFetcher(
      int id,
      const GURL& url,
      net::URLFetcher::RequestType request_type,
      net::URLFetcherDelegate* d) override {
    if (GetFetcherByID(id)) {
      LOG(WARNING) << "The ID " << id << " was already assigned to a fetcher."
                   << "Its delegate will thereforde be called right now.";
      DropAndCallDelegate(id);
    }
    fetchers_.push_back(id);
    return TestURLFetcherFactory::CreateURLFetcher(id, url, request_type, d);
  }

  // Returns the raw pointer of the last created URL fetcher.
  // If it was destroyed or no fetcher was created, it will return a nulltpr.
  net::TestURLFetcher* GetLastCreatedFetcher() {
    if (fetchers_.empty()) {
      return nullptr;
    }
    return GetFetcherByID(fetchers_.front());
  }

 private:
  // The fetcher can either be destroyed because the delegate was called during
  // execution or because we called it on destruction.
  void DropAndCallDelegate(int fetcher_id) {
    auto found_id_iter =
        std::find(fetchers_.begin(), fetchers_.end(), fetcher_id);
    if (found_id_iter == fetchers_.end()) {
      return;
    }
    fetchers_.erase(found_id_iter);
    net::TestURLFetcher* fetcher = GetFetcherByID(fetcher_id);
    if (!fetcher->delegate()) {
      return;
    }
    fetcher->delegate()->OnURLFetchComplete(fetcher);
  }

  // net::TestURLFetcherDelegateForTests overrides:
  void OnRequestStart(int fetcher_id) override {}
  void OnChunkUpload(int fetcher_id) override {}
  void OnRequestEnd(int fetcher_id) override {
    DropAndCallDelegate(fetcher_id);
  }

  std::deque<int> fetchers_;  // std::queue doesn't support std::find.
};

// Factory for FakeURLFetcher objects that always generate errors.
class FailingFakeURLFetcherFactory : public net::URLFetcherFactory {
 public:
  std::unique_ptr<net::URLFetcher> CreateURLFetcher(
      int id,
      const GURL& url,
      net::URLFetcher::RequestType request_type,
      net::URLFetcherDelegate* d) override {
    return base::MakeUnique<net::FakeURLFetcher>(
        url, d, /*response_data=*/std::string(), net::HTTP_NOT_FOUND,
        net::URLRequestStatus::FAILED);
  }
};

void ParseJson(const std::string& json,
               const SuccessCallback& success_callback,
               const ErrorCallback& error_callback) {
  base::JSONReader json_reader;
  std::unique_ptr<base::Value> value = json_reader.ReadToValue(json);
  if (value) {
    success_callback.Run(std::move(value));
  } else {
    error_callback.Run(json_reader.GetErrorMessage());
  }
}

void ParseJsonDelayed(const std::string& json,
                      const SuccessCallback& success_callback,
                      const ErrorCallback& error_callback) {
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&ParseJson, json, std::move(success_callback),
                 std::move(error_callback)),
      base::TimeDelta::FromMilliseconds(kTestJsonParsingLatencyMs));
}

}  // namespace

class RemoteSuggestionsFetcherTestBase : public testing::Test {
 public:
  explicit RemoteSuggestionsFetcherTestBase(const GURL& gurl)
      : default_variation_params_(
            {{"send_top_languages", "true"}, {"send_user_class", "true"}}),
        params_manager_(ntp_snippets::kStudyName,
                        default_variation_params_,
                        {ntp_snippets::kArticleSuggestionsFeature.name}),
        mock_task_runner_(new base::TestMockTimeTaskRunner()),
        mock_task_runner_handle_(mock_task_runner_),
        test_url_(gurl) {
    UserClassifier::RegisterProfilePrefs(utils_.pref_service()->registry());
    user_classifier_ = base::MakeUnique<UserClassifier>(utils_.pref_service());
    // Increase initial time such that ticks are non-zero.
    mock_task_runner_->FastForwardBy(base::TimeDelta::FromMilliseconds(1234));
    ResetFetcher();
  }

  void ResetFetcher() {
    scoped_refptr<net::TestURLRequestContextGetter> request_context_getter =
        new net::TestURLRequestContextGetter(mock_task_runner_.get());

    fake_token_service_ = base::MakeUnique<FakeProfileOAuth2TokenService>(
        base::MakeUnique<FakeOAuth2TokenServiceDelegate>(
            request_context_getter.get()));

    fetcher_ = base::MakeUnique<RemoteSuggestionsFetcher>(
        utils_.fake_signin_manager(), fake_token_service_.get(),
        std::move(request_context_getter), utils_.pref_service(), nullptr,
        base::Bind(&ParseJsonDelayed),
        GetFetchEndpoint(version_info::Channel::STABLE), kAPIKey,
        user_classifier_.get());

    fetcher_->SetClockForTesting(mock_task_runner_->GetMockClock());
  }

  void SignIn() { utils_.fake_signin_manager()->SignIn(kTestEmail); }

  void IssueRefreshToken() {
    fake_token_service_->GetDelegate()->UpdateCredentials(kTestEmail, "token");
  }

  void IssueOAuth2Token() {
    fake_token_service_->IssueAllTokensForAccount(kTestEmail, "access_token",
                                                  base::Time::Max());
  }

  void CancelOAuth2TokenRequests() {
    fake_token_service_->IssueErrorForAllPendingRequestsForAccount(
        kTestEmail, GoogleServiceAuthError(
                        GoogleServiceAuthError::State::REQUEST_CANCELED));
  }

  RemoteSuggestionsFetcher::SnippetsAvailableCallback
  ToSnippetsAvailableCallback(MockSnippetsAvailableCallback* callback) {
    return base::BindOnce(&MockSnippetsAvailableCallback::WrappedRun,
                          base::Unretained(callback));
  }

  RemoteSuggestionsFetcher& fetcher() { return *fetcher_; }
  MockSnippetsAvailableCallback& mock_callback() { return mock_callback_; }
  void FastForwardUntilNoTasksRemain() {
    mock_task_runner_->FastForwardUntilNoTasksRemain();
  }
  base::HistogramTester& histogram_tester() { return histogram_tester_; }

  RequestParams test_params() {
    RequestParams result;
    result.count_to_fetch = 1;
    result.interactive_request = true;
    return result;
  }

  void InitFakeURLFetcherFactory() {
    if (fake_url_fetcher_factory_) {
      return;
    }
    // Instantiation of factory automatically sets itself as URLFetcher's
    // factory.
    fake_url_fetcher_factory_.reset(new net::FakeURLFetcherFactory(
        /*default_factory=*/&failing_url_fetcher_factory_));
  }

  void SetVariationParam(std::string param_name, std::string value) {
    std::map<std::string, std::string> params = default_variation_params_;
    params[param_name] = value;

    params_manager_.ClearAllVariationParams();
    params_manager_.SetVariationParamsWithFeatureAssociations(
        ntp_snippets::kStudyName, params,
        {ntp_snippets::kArticleSuggestionsFeature.name});
  }

  void SetFakeResponse(const std::string& response_data,
                       net::HttpStatusCode response_code,
                       net::URLRequestStatus::Status status) {
    InitFakeURLFetcherFactory();
    fake_url_fetcher_factory_->SetFakeResponse(test_url_, response_data,
                                               response_code, status);
  }

 protected:
  std::map<std::string, std::string> default_variation_params_;

 private:
  test::RemoteSuggestionsTestUtils utils_;
  variations::testing::VariationParamsManager params_manager_;
  scoped_refptr<base::TestMockTimeTaskRunner> mock_task_runner_;
  base::ThreadTaskRunnerHandle mock_task_runner_handle_;
  FailingFakeURLFetcherFactory failing_url_fetcher_factory_;
  // Initialized lazily in SetFakeResponse().
  std::unique_ptr<net::FakeURLFetcherFactory> fake_url_fetcher_factory_;
  std::unique_ptr<FakeProfileOAuth2TokenService> fake_token_service_;
  std::unique_ptr<RemoteSuggestionsFetcher> fetcher_;
  std::unique_ptr<UserClassifier> user_classifier_;
  MockSnippetsAvailableCallback mock_callback_;
  const GURL test_url_;
  base::HistogramTester histogram_tester_;

  DISALLOW_COPY_AND_ASSIGN(RemoteSuggestionsFetcherTestBase);
};

class RemoteSuggestionsChromeReaderFetcherTest
    : public RemoteSuggestionsFetcherTestBase {
 public:
  RemoteSuggestionsChromeReaderFetcherTest()
      : RemoteSuggestionsFetcherTestBase(GURL(kTestChromeReaderUrl)) {
    default_variation_params_["content_suggestions_backend"] =
        kChromeReaderServer;
    SetVariationParam("content_suggestions_backend", kChromeReaderServer);
    ResetFetcher();
  }
};

class RemoteSuggestionsSignedOutFetcherTest
    : public RemoteSuggestionsFetcherTestBase {
 public:
  RemoteSuggestionsSignedOutFetcherTest()
      : RemoteSuggestionsFetcherTestBase(
            GURL(kTestChromeContentSuggestionsSignedOutUrl)) {}
};

// TODO(jkrcal): Add unit-tests for the "authentication in progress" case as it
// requires more changes (instead FakeSigninManagerBase use FakeSigninManager
// which does not exist on ChromeOS). crbug.com/688310
class RemoteSuggestionsSignedInFetcherTest
    : public RemoteSuggestionsFetcherTestBase {
 public:
  RemoteSuggestionsSignedInFetcherTest()
      : RemoteSuggestionsFetcherTestBase(
            GURL(kTestChromeContentSuggestionsSignedInUrl)) {}
};

TEST_F(RemoteSuggestionsChromeReaderFetcherTest, ShouldNotFetchOnCreation) {
  // The lack of registered baked in responses would cause any fetch to fail.
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              IsEmpty());
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              IsEmpty());
  EXPECT_THAT(fetcher().last_status(), IsEmpty());
}

TEST_F(RemoteSuggestionsChromeReaderFetcherTest, ShouldFetchSuccessfully) {
  const std::string kJsonStr =
      "{\"recos\": [{"
      "  \"contentInfo\": {"
      "    \"url\" : \"http://localhost/foobar\","
      "    \"sourceCorpusInfo\" : [{"
      "      \"ampUrl\" : \"http://localhost/amp\","
      "      \"corpusId\" : \"http://localhost/foobar\","
      "      \"publisherData\": { \"sourceName\" : \"Foo News\" }"
      "    }]"
      "  }"
      "}]}";
  SetFakeResponse(/*response_data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(),
              Run(IsSuccess(),
                  AllOf(IsSingleArticle("http://localhost/foobar"),
                        FirstCategoryHasInfo(IsCategoryInfoForArticles()))));
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(fetcher().last_status(), Eq("OK"));
  EXPECT_THAT(fetcher().last_json(), Eq(kJsonStr));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              ElementsAre(base::Bucket(/*min=*/kTestJsonParsingLatencyMs,
                                       /*count=*/1)));
}

TEST_F(RemoteSuggestionsSignedOutFetcherTest, ShouldFetchSuccessfully) {
  const std::string kJsonStr =
      "{\"categories\" : [{"
      "  \"id\": 1,"
      "  \"localizedTitle\": \"Articles for You\","
      "  \"suggestions\" : [{"
      "    \"ids\" : [\"http://localhost/foobar\"],"
      "    \"title\" : \"Foo Barred from Baz\","
      "    \"snippet\" : \"...\","
      "    \"fullPageUrl\" : \"http://localhost/foobar\","
      "    \"creationTime\" : \"2016-06-30T11:01:37.000Z\","
      "    \"expirationTime\" : \"2016-07-01T11:01:37.000Z\","
      "    \"attribution\" : \"Foo News\","
      "    \"imageUrl\" : \"http://localhost/foobar.jpg\","
      "    \"ampUrl\" : \"http://localhost/amp\","
      "    \"faviconUrl\" : \"http://localhost/favicon.ico\" "
      "  }]"
      "}]}";
  SetFakeResponse(/*response_data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(),
              Run(IsSuccess(),
                  AllOf(IsSingleArticle("http://localhost/foobar"),
                        FirstCategoryHasInfo(IsCategoryInfoForArticles()))));
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(fetcher().last_status(), Eq("OK"));
  EXPECT_THAT(fetcher().last_json(), Eq(kJsonStr));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              ElementsAre(base::Bucket(/*min=*/kTestJsonParsingLatencyMs,
                                       /*count=*/1)));
}

TEST_F(RemoteSuggestionsSignedInFetcherTest, ShouldFetchSuccessfully) {
  SignIn();
  IssueRefreshToken();

  const std::string kJsonStr =
      "{\"categories\" : [{"
      "  \"id\": 1,"
      "  \"localizedTitle\": \"Articles for You\","
      "  \"suggestions\" : [{"
      "    \"ids\" : [\"http://localhost/foobar\"],"
      "    \"title\" : \"Foo Barred from Baz\","
      "    \"snippet\" : \"...\","
      "    \"fullPageUrl\" : \"http://localhost/foobar\","
      "    \"creationTime\" : \"2016-06-30T11:01:37.000Z\","
      "    \"expirationTime\" : \"2016-07-01T11:01:37.000Z\","
      "    \"attribution\" : \"Foo News\","
      "    \"imageUrl\" : \"http://localhost/foobar.jpg\","
      "    \"ampUrl\" : \"http://localhost/amp\","
      "    \"faviconUrl\" : \"http://localhost/favicon.ico\" "
      "  }]"
      "}]}";
  SetFakeResponse(/*response_data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(),
              Run(IsSuccess(),
                  AllOf(IsSingleArticle("http://localhost/foobar"),
                        FirstCategoryHasInfo(IsCategoryInfoForArticles()))));

  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));

  IssueOAuth2Token();
  // Wait for the fake response.
  FastForwardUntilNoTasksRemain();

  EXPECT_THAT(fetcher().last_status(), Eq("OK"));
  EXPECT_THAT(fetcher().last_json(), Eq(kJsonStr));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              ElementsAre(base::Bucket(/*min=*/kTestJsonParsingLatencyMs,
                                       /*count=*/1)));
}

TEST_F(RemoteSuggestionsSignedInFetcherTest,
       ShouldRetryWhenOAuthCancelled) {
  SignIn();
  IssueRefreshToken();

  const std::string kJsonStr =
      "{\"categories\" : [{"
      "  \"id\": 1,"
      "  \"localizedTitle\": \"Articles for You\","
      "  \"suggestions\" : [{"
      "    \"ids\" : [\"http://localhost/foobar\"],"
      "    \"title\" : \"Foo Barred from Baz\","
      "    \"snippet\" : \"...\","
      "    \"fullPageUrl\" : \"http://localhost/foobar\","
      "    \"creationTime\" : \"2016-06-30T11:01:37.000Z\","
      "    \"expirationTime\" : \"2016-07-01T11:01:37.000Z\","
      "    \"attribution\" : \"Foo News\","
      "    \"imageUrl\" : \"http://localhost/foobar.jpg\","
      "    \"ampUrl\" : \"http://localhost/amp\","
      "    \"faviconUrl\" : \"http://localhost/favicon.ico\" "
      "  }]"
      "}]}";
  SetFakeResponse(/*response_data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(),
              Run(IsSuccess(),
                  AllOf(IsSingleArticle("http://localhost/foobar"),
                        FirstCategoryHasInfo(IsCategoryInfoForArticles()))));

  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));

  CancelOAuth2TokenRequests();
  IssueOAuth2Token();
  // Wait for the fake response.
  FastForwardUntilNoTasksRemain();

  EXPECT_THAT(fetcher().last_status(), Eq("OK"));
  EXPECT_THAT(fetcher().last_json(), Eq(kJsonStr));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              ElementsAre(base::Bucket(/*min=*/kTestJsonParsingLatencyMs,
                                       /*count=*/1)));
}

TEST_F(RemoteSuggestionsSignedOutFetcherTest, EmptyCategoryIsOK) {
  const std::string kJsonStr =
      "{\"categories\" : [{"
      "  \"id\": 1,"
      "  \"localizedTitle\": \"Articles for You\""
      "}]}";
  SetFakeResponse(/*response_data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(IsSuccess(), IsEmptyArticleList()));
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(fetcher().last_status(), Eq("OK"));
  EXPECT_THAT(fetcher().last_json(), Eq(kJsonStr));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              ElementsAre(base::Bucket(/*min=*/kTestJsonParsingLatencyMs,
                                       /*count=*/1)));
}

TEST_F(RemoteSuggestionsSignedOutFetcherTest, ServerCategories) {
  const std::string kJsonStr =
      "{\"categories\" : [{"
      "  \"id\": 1,"
      "  \"localizedTitle\": \"Articles for You\","
      "  \"suggestions\" : [{"
      "    \"ids\" : [\"http://localhost/foobar\"],"
      "    \"title\" : \"Foo Barred from Baz\","
      "    \"snippet\" : \"...\","
      "    \"fullPageUrl\" : \"http://localhost/foobar\","
      "    \"creationTime\" : \"2016-06-30T11:01:37.000Z\","
      "    \"expirationTime\" : \"2016-07-01T11:01:37.000Z\","
      "    \"attribution\" : \"Foo News\","
      "    \"imageUrl\" : \"http://localhost/foobar.jpg\","
      "    \"ampUrl\" : \"http://localhost/amp\","
      "    \"faviconUrl\" : \"http://localhost/favicon.ico\" "
      "  }]"
      "}, {"
      "  \"id\": 2,"
      "  \"localizedTitle\": \"Articles for Me\","
      "  \"allowFetchingMoreResults\": true,"
      "  \"suggestions\" : [{"
      "    \"ids\" : [\"http://localhost/foo2\"],"
      "    \"title\" : \"Foo Barred from Baz\","
      "    \"snippet\" : \"...\","
      "    \"fullPageUrl\" : \"http://localhost/foo2\","
      "    \"creationTime\" : \"2016-06-30T11:01:37.000Z\","
      "    \"expirationTime\" : \"2016-07-01T11:01:37.000Z\","
      "    \"attribution\" : \"Foo News\","
      "    \"imageUrl\" : \"http://localhost/foo2.jpg\","
      "    \"ampUrl\" : \"http://localhost/amp\","
      "    \"faviconUrl\" : \"http://localhost/favicon.ico\" "
      "  }]"
      "}]}";
  SetFakeResponse(/*response_data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  RemoteSuggestionsFetcher::OptionalFetchedCategories fetched_categories;
  EXPECT_CALL(mock_callback(), Run(IsSuccess(), _))
      .WillOnce(MoveArgument1PointeeTo(&fetched_categories));
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();

  ASSERT_TRUE(fetched_categories);
  ASSERT_THAT(fetched_categories->size(), Eq(2u));
  for (const auto& category : *fetched_categories) {
    const auto& articles = category.suggestions;
    if (category.category.IsKnownCategory(KnownCategories::ARTICLES)) {
      ASSERT_THAT(articles.size(), Eq(1u));
      EXPECT_THAT(articles[0]->url().spec(), Eq("http://localhost/foobar"));
      EXPECT_THAT(category.info, IsCategoryInfoForArticles());
    } else if (category.category == Category::FromRemoteCategory(2)) {
      ASSERT_THAT(articles.size(), Eq(1u));
      EXPECT_THAT(articles[0]->url().spec(), Eq("http://localhost/foo2"));
      EXPECT_THAT(category.info.has_fetch_action(), Eq(true));
      EXPECT_THAT(category.info.has_view_all_action(), Eq(false));
      EXPECT_THAT(category.info.show_if_empty(), Eq(false));
    } else {
      FAIL() << "unknown category ID " << category.category.id();
    }
  }

  EXPECT_THAT(fetcher().last_status(), Eq("OK"));
  EXPECT_THAT(fetcher().last_json(), Eq(kJsonStr));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              ElementsAre(base::Bucket(/*min=*/kTestJsonParsingLatencyMs,
                                       /*count=*/1)));
}

TEST_F(RemoteSuggestionsSignedOutFetcherTest,
       SupportMissingAllowFetchingMoreResultsOption) {
  // This tests makes sure we handle the missing option although it's required
  // by the interface. It's just that the Service doesn't follow that
  // requirement (yet). TODO(tschumann): remove this test once not needed
  // anymore.
  const std::string kJsonStr =
      "{\"categories\" : [{"
      "  \"id\": 2,"
      "  \"localizedTitle\": \"Articles for Me\","
      "  \"suggestions\" : [{"
      "    \"ids\" : [\"http://localhost/foo2\"],"
      "    \"title\" : \"Foo Barred from Baz\","
      "    \"snippet\" : \"...\","
      "    \"fullPageUrl\" : \"http://localhost/foo2\","
      "    \"creationTime\" : \"2016-06-30T11:01:37.000Z\","
      "    \"expirationTime\" : \"2016-07-01T11:01:37.000Z\","
      "    \"attribution\" : \"Foo News\","
      "    \"imageUrl\" : \"http://localhost/foo2.jpg\","
      "    \"ampUrl\" : \"http://localhost/amp\","
      "    \"faviconUrl\" : \"http://localhost/favicon.ico\" "
      "  }]"
      "}]}";
  SetFakeResponse(/*response_data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  RemoteSuggestionsFetcher::OptionalFetchedCategories fetched_categories;
  EXPECT_CALL(mock_callback(), Run(IsSuccess(), _))
      .WillOnce(MoveArgument1PointeeTo(&fetched_categories));
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();

  ASSERT_TRUE(fetched_categories);
  ASSERT_THAT(fetched_categories->size(), Eq(1u));
  EXPECT_THAT(fetched_categories->front().info.has_fetch_action(), Eq(false));
  EXPECT_THAT(fetched_categories->front().info.title(),
              Eq(base::UTF8ToUTF16("Articles for Me")));
}

TEST_F(RemoteSuggestionsSignedOutFetcherTest, ExclusiveCategoryOnly) {
  const std::string kJsonStr =
      "{\"categories\" : [{"
      "  \"id\": 1,"
      "  \"localizedTitle\": \"Articles for You\","
      "  \"suggestions\" : [{"
      "    \"ids\" : [\"http://localhost/foobar\"],"
      "    \"title\" : \"Foo Barred from Baz\","
      "    \"snippet\" : \"...\","
      "    \"fullPageUrl\" : \"http://localhost/foobar\","
      "    \"creationTime\" : \"2016-06-30T11:01:37.000Z\","
      "    \"expirationTime\" : \"2016-07-01T11:01:37.000Z\","
      "    \"attribution\" : \"Foo News\","
      "    \"imageUrl\" : \"http://localhost/foobar.jpg\","
      "    \"ampUrl\" : \"http://localhost/amp\","
      "    \"faviconUrl\" : \"http://localhost/favicon.ico\" "
      "  }]"
      "}, {"
      "  \"id\": 2,"
      "  \"localizedTitle\": \"Articles for Me\","
      "  \"suggestions\" : [{"
      "    \"ids\" : [\"http://localhost/foo2\"],"
      "    \"title\" : \"Foo Barred from Baz\","
      "    \"snippet\" : \"...\","
      "    \"fullPageUrl\" : \"http://localhost/foo2\","
      "    \"creationTime\" : \"2016-06-30T11:01:37.000Z\","
      "    \"expirationTime\" : \"2016-07-01T11:01:37.000Z\","
      "    \"attribution\" : \"Foo News\","
      "    \"imageUrl\" : \"http://localhost/foo2.jpg\","
      "    \"ampUrl\" : \"http://localhost/amp\","
      "    \"faviconUrl\" : \"http://localhost/favicon.ico\" "
      "  }]"
      "}, {"
      "  \"id\": 3,"
      "  \"localizedTitle\": \"Articles for Anybody\","
      "  \"suggestions\" : [{"
      "    \"ids\" : [\"http://localhost/foo3\"],"
      "    \"title\" : \"Foo Barred from Baz\","
      "    \"snippet\" : \"...\","
      "    \"fullPageUrl\" : \"http://localhost/foo3\","
      "    \"creationTime\" : \"2016-06-30T11:01:37.000Z\","
      "    \"expirationTime\" : \"2016-07-01T11:01:37.000Z\","
      "    \"attribution\" : \"Foo News\","
      "    \"imageUrl\" : \"http://localhost/foo3.jpg\","
      "    \"ampUrl\" : \"http://localhost/amp\","
      "    \"faviconUrl\" : \"http://localhost/favicon.ico\" "
      "  }]"
      "}]}";
  SetFakeResponse(/*response_data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  RemoteSuggestionsFetcher::OptionalFetchedCategories fetched_categories;
  EXPECT_CALL(mock_callback(), Run(IsSuccess(), _))
      .WillOnce(MoveArgument1PointeeTo(&fetched_categories));

  RequestParams params = test_params();
  params.exclusive_category =
      base::Optional<Category>(Category::FromRemoteCategory(2));

  fetcher().FetchSnippets(params,
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();

  ASSERT_TRUE(fetched_categories);
  ASSERT_THAT(fetched_categories->size(), Eq(1u));
  const auto& category = (*fetched_categories)[0];
  EXPECT_THAT(category.category.id(), Eq(Category::FromRemoteCategory(2).id()));
  ASSERT_THAT(category.suggestions.size(), Eq(1u));
  EXPECT_THAT(category.suggestions[0]->url().spec(),
              Eq("http://localhost/foo2"));
}

TEST_F(RemoteSuggestionsChromeReaderFetcherTest,
       ShouldFetchSuccessfullyEmptyList) {
  const std::string kJsonStr = "{\"recos\": []}";
  SetFakeResponse(/*response_data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(IsSuccess(), IsEmptyArticleList()));
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(fetcher().last_status(), Eq("OK"));
  EXPECT_THAT(fetcher().last_json(), Eq(kJsonStr));
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/0, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
}

TEST_F(RemoteSuggestionsChromeReaderFetcherTest, RetryOnInteractiveRequests) {
  DelegateCallingTestURLFetcherFactory fetcher_factory;
  RequestParams params = test_params();
  params.interactive_request = true;

  fetcher().FetchSnippets(params,
                          ToSnippetsAvailableCallback(&mock_callback()));

  net::TestURLFetcher* fetcher = fetcher_factory.GetLastCreatedFetcher();
  ASSERT_THAT(fetcher, NotNull());
  EXPECT_THAT(fetcher->GetMaxRetriesOn5xx(), Eq(2));
}

TEST_F(RemoteSuggestionsChromeReaderFetcherTest,
       RetriesConfigurableOnNonInteractiveRequests) {
  struct ExpectationForVariationParam {
    std::string param_value;
    int expected_value;
    std::string description;
  };
  const std::vector<ExpectationForVariationParam> retry_config_expectation = {
      {"", 0, "Do not retry by default"},
      {"0", 0, "Do not retry on param value 0"},
      {"-1", 0, "Do not retry on negative param values."},
      {"4", 4, "Retry as set in param value."}};

  RequestParams params = test_params();
  params.interactive_request = false;

  for (const auto& retry_config : retry_config_expectation) {
    DelegateCallingTestURLFetcherFactory fetcher_factory;
    SetVariationParam("background_5xx_retries_count", retry_config.param_value);

    fetcher().FetchSnippets(params,
                            ToSnippetsAvailableCallback(&mock_callback()));

    net::TestURLFetcher* fetcher = fetcher_factory.GetLastCreatedFetcher();
    ASSERT_THAT(fetcher, NotNull());
    EXPECT_THAT(fetcher->GetMaxRetriesOn5xx(), Eq(retry_config.expected_value))
        << retry_config.description;
  }
}

TEST_F(RemoteSuggestionsChromeReaderFetcherTest, ShouldReportUrlStatusError) {
  SetFakeResponse(/*response_data=*/std::string(), net::HTTP_NOT_FOUND,
                  net::URLRequestStatus::FAILED);
  EXPECT_CALL(mock_callback(), Run(HasCode(StatusCode::TEMPORARY_ERROR),
                                   /*snippets=*/Not(HasValue())))
      .Times(1);
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(fetcher().last_status(), Eq("URLRequestStatus error -2"));
  EXPECT_THAT(fetcher().last_json(), IsEmpty());
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/2, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/-2, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              Not(IsEmpty()));
}

TEST_F(RemoteSuggestionsChromeReaderFetcherTest, ShouldReportHttpError) {
  SetFakeResponse(/*response_data=*/std::string(), net::HTTP_NOT_FOUND,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(HasCode(StatusCode::TEMPORARY_ERROR),
                                   /*snippets=*/Not(HasValue())))
      .Times(1);
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(fetcher().last_json(), IsEmpty());
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/3, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/404, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              Not(IsEmpty()));
}

TEST_F(RemoteSuggestionsChromeReaderFetcherTest, ShouldReportJsonError) {
  const std::string kInvalidJsonStr = "{ \"recos\": []";
  SetFakeResponse(/*response_data=*/kInvalidJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(HasCode(StatusCode::TEMPORARY_ERROR),
                                   /*snippets=*/Not(HasValue())))
      .Times(1);
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(fetcher().last_status(),
              StartsWith("Received invalid JSON (error "));
  EXPECT_THAT(fetcher().last_json(), Eq(kInvalidJsonStr));
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/4, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              ElementsAre(base::Bucket(/*min=*/kTestJsonParsingLatencyMs,
                                       /*count=*/1)));
}

TEST_F(RemoteSuggestionsChromeReaderFetcherTest,
       ShouldReportJsonErrorForEmptyResponse) {
  SetFakeResponse(/*response_data=*/std::string(), net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(HasCode(StatusCode::TEMPORARY_ERROR),
                                   /*snippets=*/Not(HasValue())))
      .Times(1);
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(fetcher().last_json(), std::string());
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/4, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
}

TEST_F(RemoteSuggestionsChromeReaderFetcherTest, ShouldReportInvalidListError) {
  const std::string kJsonStr =
      "{\"recos\": [{ \"contentInfo\": { \"foo\" : \"bar\" }}]}";
  SetFakeResponse(/*response_data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(HasCode(StatusCode::TEMPORARY_ERROR),
                                   /*snippets=*/Not(HasValue())))
      .Times(1);
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(fetcher().last_json(), Eq(kJsonStr));
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/5, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              Not(IsEmpty()));
}

// This test actually verifies that the test setup itself is sane, to prevent
// hard-to-reproduce test failures.
TEST_F(RemoteSuggestionsChromeReaderFetcherTest,
       ShouldReportHttpErrorForMissingBakedResponse) {
  InitFakeURLFetcherFactory();
  EXPECT_CALL(mock_callback(), Run(HasCode(StatusCode::TEMPORARY_ERROR),
                                   /*snippets=*/Not(HasValue())))
      .Times(1);
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();
}

TEST_F(RemoteSuggestionsChromeReaderFetcherTest,
       ShouldProcessConcurrentFetches) {
  const std::string kJsonStr = "{ \"recos\": [] }";
  SetFakeResponse(/*response_data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(IsSuccess(), IsEmptyArticleList())).Times(5);
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  // More calls to FetchSnippets() do not interrupt the previous.
  // Callback is expected to be called once each time.
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  fetcher().FetchSnippets(test_params(),
                          ToSnippetsAvailableCallback(&mock_callback()));
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/0, /*count=*/5)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/5)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              ElementsAre(base::Bucket(/*min=*/kTestJsonParsingLatencyMs,
                                       /*count=*/5)));
}

::std::ostream& operator<<(
    ::std::ostream& os,
    const RemoteSuggestionsFetcher::OptionalFetchedCategories&
        fetched_categories) {
  if (fetched_categories) {
    // Matchers above aren't any more precise than this, so this is sufficient
    // for test-failure diagnostics.
    return os << "list with " << fetched_categories->size() << " elements";
  }
  return os << "null";
}

}  // namespace ntp_snippets
