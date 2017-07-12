// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "headless/lib/browser/headless_devtools_manager_delegate.h"

#include <string>
#include <utility>

#include "content/public/browser/browser_thread.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_frontend_host.h"
#include "content/public/browser/web_contents.h"
#include "headless/grit/headless_lib_resources.h"
#include "headless/lib/browser/headless_browser_context_impl.h"
#include "headless/lib/browser/headless_browser_impl.h"
#include "headless/lib/browser/headless_web_contents_impl.h"
#include "headless/public/devtools/domains/target.h"
#include "printing/features/features.h"
#include "ui/base/resource/resource_bundle.h"

#if BUILDFLAG(ENABLE_BASIC_PRINTING)
#include "headless/lib/browser/headless_print_manager.h"
#endif

namespace headless {

namespace {
const char kIdParam[] = "id";
const char kResultParam[] = "result";
const char kErrorParam[] = "error";
const char kErrorCodeParam[] = "code";
const char kErrorMessageParam[] = "message";

// JSON RPC 2.0 spec: http://www.jsonrpc.org/specification#error_object
enum Error {
  kErrorInvalidParam = -32602,
  kErrorServerError = -32000
};

std::unique_ptr<base::DictionaryValue> CreateSuccessResponse(
    int command_id,
    std::unique_ptr<base::Value> result) {
  if (!result)
    result = base::MakeUnique<base::DictionaryValue>();

  auto response = base::MakeUnique<base::DictionaryValue>();
  response->SetInteger(kIdParam, command_id);
  response->Set(kResultParam, std::move(result));
  return response;
}

std::unique_ptr<base::DictionaryValue> CreateErrorResponse(
    int command_id,
    int error_code,
    const std::string& error_message) {
  auto error_object = base::MakeUnique<base::DictionaryValue>();
  error_object->SetInteger(kErrorCodeParam, error_code);
  error_object->SetString(kErrorMessageParam, error_message);

  auto response = base::MakeUnique<base::DictionaryValue>();
  response->SetInteger(kIdParam, command_id);
  response->Set(kErrorParam, std::move(error_object));
  return response;
}

std::unique_ptr<base::DictionaryValue> CreateInvalidParamResponse(
    int command_id,
    const std::string& param) {
  return CreateErrorResponse(
      command_id, kErrorInvalidParam,
      base::StringPrintf("Missing or invalid '%s' parameter", param.c_str()));
}

#if BUILDFLAG(ENABLE_BASIC_PRINTING)
void PDFCreated(
    const content::DevToolsManagerDelegate::CommandCallback& callback,
    int command_id,
    printing::HeadlessPrintManager::PrintResult print_result,
    const std::string& data) {
  std::unique_ptr<base::DictionaryValue> response;
  if (print_result == printing::HeadlessPrintManager::PRINT_SUCCESS) {
    response = CreateSuccessResponse(
        command_id,
        printing::HeadlessPrintManager::PDFContentsToDictionaryValue(data));
  } else {
    response = CreateErrorResponse(
        command_id, kErrorServerError,
        printing::HeadlessPrintManager::PrintResultToString(print_result));
  }
  callback.Run(std::move(response));
}
#endif

}  // namespace

HeadlessDevToolsManagerDelegate::HeadlessDevToolsManagerDelegate(
    base::WeakPtr<HeadlessBrowserImpl> browser)
    : browser_(std::move(browser)) {
  command_map_["Target.createTarget"] = base::Bind(
      &HeadlessDevToolsManagerDelegate::CreateTarget, base::Unretained(this));
  command_map_["Target.closeTarget"] = base::Bind(
      &HeadlessDevToolsManagerDelegate::CloseTarget, base::Unretained(this));
  command_map_["Target.createBrowserContext"] =
      base::Bind(&HeadlessDevToolsManagerDelegate::CreateBrowserContext,
                 base::Unretained(this));
  command_map_["Target.disposeBrowserContext"] =
      base::Bind(&HeadlessDevToolsManagerDelegate::DisposeBrowserContext,
                 base::Unretained(this));

  async_command_map_["Page.printToPDF"] = base::Bind(
      &HeadlessDevToolsManagerDelegate::PrintToPDF, base::Unretained(this));
}

HeadlessDevToolsManagerDelegate::~HeadlessDevToolsManagerDelegate() {}

base::DictionaryValue* HeadlessDevToolsManagerDelegate::HandleCommand(
    content::DevToolsAgentHost* agent_host,
    base::DictionaryValue* command) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (!browser_)
    return nullptr;

  int id;
  std::string method;
  if (!command->GetInteger("id", &id) || !command->GetString("method", &method))
    return nullptr;

  auto find_it = command_map_.find(method);
  if (find_it == command_map_.end())
    return nullptr;

  const base::DictionaryValue* params = nullptr;
  command->GetDictionary("params", &params);
  auto cmd_result = find_it->second.Run(id, params);
  return cmd_result.release();
}

bool HeadlessDevToolsManagerDelegate::HandleAsyncCommand(
    content::DevToolsAgentHost* agent_host,
    base::DictionaryValue* command,
    const CommandCallback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (!browser_)
    return false;

  int id;
  std::string method;
  if (!command->GetInteger("id", &id) || !command->GetString("method", &method))
    return false;

  auto find_it = async_command_map_.find(method);
  if (find_it == async_command_map_.end())
    return false;

  const base::DictionaryValue* params = nullptr;
  command->GetDictionary("params", &params);
  find_it->second.Run(agent_host, id, params, callback);
  return true;
}

scoped_refptr<content::DevToolsAgentHost>
HeadlessDevToolsManagerDelegate::CreateNewTarget(const GURL& url) {
  HeadlessBrowserContext* context = browser_->GetDefaultBrowserContext();
  HeadlessWebContentsImpl* web_contents_impl = HeadlessWebContentsImpl::From(
      context->CreateWebContentsBuilder()
          .SetInitialURL(url)
          .SetWindowSize(browser_->options()->window_size)
          .Build());
  return content::DevToolsAgentHost::GetOrCreateFor(
      web_contents_impl->web_contents());
}

std::string HeadlessDevToolsManagerDelegate::GetDiscoveryPageHTML() {
  return ResourceBundle::GetSharedInstance()
      .GetRawDataResource(IDR_HEADLESS_LIB_DEVTOOLS_DISCOVERY_PAGE)
      .as_string();
}

std::string HeadlessDevToolsManagerDelegate::GetFrontendResource(
    const std::string& path) {
  return content::DevToolsFrontendHost::GetFrontendResource(path).as_string();
}

void HeadlessDevToolsManagerDelegate::PrintToPDF(
    content::DevToolsAgentHost* agent_host,
    int command_id,
    const base::DictionaryValue* params,
    const CommandCallback& callback) {
#if BUILDFLAG(ENABLE_BASIC_PRINTING)
  content::WebContents* web_contents = agent_host->GetWebContents();
  content::RenderFrameHost* rfh = web_contents->GetMainFrame();

  printing::HeadlessPrintManager::FromWebContents(web_contents)
      ->GetPDFContents(rfh, base::Bind(&PDFCreated, callback, command_id));
#else
  DCHECK(callback);
  callback.Run(CreateErrorResponse(command_id, kErrorServerError,
                                   "Printing is not enabled"));
#endif
}

std::unique_ptr<base::DictionaryValue>
HeadlessDevToolsManagerDelegate::CreateTarget(
    int command_id,
    const base::DictionaryValue* params) {
  std::string url;
  std::string browser_context_id;
  int width = browser_->options()->window_size.width();
  int height = browser_->options()->window_size.height();
  if (!params || !params->GetString("url", &url))
    return CreateInvalidParamResponse(command_id, "url");
  params->GetString("browserContextId", &browser_context_id);
  params->GetInteger("width", &width);
  params->GetInteger("height", &height);

  HeadlessBrowserContext* context =
      browser_->GetBrowserContextForId(browser_context_id);
  if (!browser_context_id.empty()) {
    context = browser_->GetBrowserContextForId(browser_context_id);
    if (!context)
      return CreateInvalidParamResponse(command_id, "browserContextId");
  } else {
    context = browser_->GetDefaultBrowserContext();
    if (!context) {
      return CreateErrorResponse(command_id, kErrorServerError,
                                 "You specified no |browserContextId|, but "
                                 "there is no default browser context set on "
                                 "HeadlessBrowser");
    }
  }

  HeadlessWebContentsImpl* web_contents_impl =
      HeadlessWebContentsImpl::From(context->CreateWebContentsBuilder()
                                        .SetInitialURL(GURL(url))
                                        .SetWindowSize(gfx::Size(width, height))
                                        .Build());

  std::unique_ptr<base::Value> result(
      target::CreateTargetResult::Builder()
          .SetTargetId(web_contents_impl->GetDevToolsAgentHostId())
          .Build()
          ->Serialize());
  return CreateSuccessResponse(command_id, std::move(result));
}

std::unique_ptr<base::DictionaryValue>
HeadlessDevToolsManagerDelegate::CloseTarget(
    int command_id,
    const base::DictionaryValue* params) {
  std::string target_id;
  if (!params || !params->GetString("targetId", &target_id))
    return CreateInvalidParamResponse(command_id, "targetId");
  HeadlessWebContents* web_contents =
      browser_->GetWebContentsForDevToolsAgentHostId(target_id);
  bool success = false;
  if (web_contents) {
    web_contents->Close();
    success = true;
  }
  std::unique_ptr<base::Value> result(target::CloseTargetResult::Builder()
                                          .SetSuccess(success)
                                          .Build()
                                          ->Serialize());
  return CreateSuccessResponse(command_id, std::move(result));
}

std::unique_ptr<base::DictionaryValue>
HeadlessDevToolsManagerDelegate::CreateBrowserContext(
    int command_id,
    const base::DictionaryValue* params) {
  HeadlessBrowserContext* browser_context =
      browser_->CreateBrowserContextBuilder().Build();

  std::unique_ptr<base::Value> result(
      target::CreateBrowserContextResult::Builder()
          .SetBrowserContextId(browser_context->Id())
          .Build()
          ->Serialize());
  return CreateSuccessResponse(command_id, std::move(result));
}

std::unique_ptr<base::DictionaryValue>
HeadlessDevToolsManagerDelegate::DisposeBrowserContext(
    int command_id,
    const base::DictionaryValue* params) {
  std::string browser_context_id;
  if (!params || !params->GetString("browserContextId", &browser_context_id))
    return CreateInvalidParamResponse(command_id, "browserContextId");
  HeadlessBrowserContext* context =
      browser_->GetBrowserContextForId(browser_context_id);

  bool success = false;
  if (context && context != browser_->GetDefaultBrowserContext() &&
      context->GetAllWebContents().empty()) {
    success = true;
    context->Close();
  }

  std::unique_ptr<base::Value> result(
      target::DisposeBrowserContextResult::Builder()
          .SetSuccess(success)
          .Build()
          ->Serialize());
  return CreateSuccessResponse(command_id, std::move(result));
}

}  // namespace headless
