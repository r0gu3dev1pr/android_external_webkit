/*
 * Copyright 2010, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_TAG "WebUrlLoaderClient"

#include "config.h"
#include "WebUrlLoaderClient.h"

#include "ChromiumIncludes.h"
#include "OwnPtr.h"
#include "ResourceHandle.h"
#include "ResourceHandleClient.h"
#include "ResourceResponse.h"
#include "UserGestureIndicator.h"
#include "WebCoreFrameBridge.h"
#include "WebRequest.h"
#include "WebResourceRequest.h"

#include <wtf/text/CString.h>

namespace android {

base::Thread* WebUrlLoaderClient::ioThread()
{
    static base::Thread* networkThread = 0;
    static Lock networkThreadLock;

    // Multiple threads appear to access the ioThread so we must ensure the
    // critical section ordering.
    AutoLock lock(networkThreadLock);

    if (!networkThread)
        networkThread = new base::Thread("network");

    if (!networkThread)
        return 0;

    if (networkThread->IsRunning())
        return networkThread;

    base::Thread::Options options;
    options.message_loop_type = MessageLoop::TYPE_IO;
    if (!networkThread->StartWithOptions(options)) {
        delete networkThread;
        networkThread = 0;
    }

    return networkThread;
}

Lock* WebUrlLoaderClient::syncLock() {
    static Lock s_syncLock;
    return &s_syncLock;
}

ConditionVariable* WebUrlLoaderClient::syncCondition() {
    static ConditionVariable s_syncCondition(syncLock());
    return &s_syncCondition;
}

WebUrlLoaderClient::~WebUrlLoaderClient()
{
}

bool WebUrlLoaderClient::isActive() const
{
    if (m_cancelling)
        return false;
    if (!m_resourceHandle->client())
        return false;

    return true;
}

WebUrlLoaderClient::WebUrlLoaderClient(WebFrame* webFrame, WebCore::ResourceHandle* resourceHandle, const WebCore::ResourceRequest& resourceRequest)
    : m_webFrame(webFrame)
    , m_resourceHandle(resourceHandle)
    , m_cancelling(false)
    , m_sync(false)
    , m_finished(false)
{
    WebResourceRequest webResourceRequest(resourceRequest);
    UrlInterceptResponse* intercept = webFrame->shouldInterceptRequest(resourceRequest.url().string());
    if (intercept) {
        m_request = new WebRequest(this, webResourceRequest, intercept);
        return;
    }

    m_request = new WebRequest(this, webResourceRequest);

    // Set uploads before start is called on the request
    if (resourceRequest.httpBody() && !(webResourceRequest.method() == "GET" || webResourceRequest.method() == "HEAD")) {
        Vector<FormDataElement>::iterator iter;
        Vector<FormDataElement> elements = resourceRequest.httpBody()->elements();
        for (iter = elements.begin(); iter != elements.end(); iter++) {
            FormDataElement element = *iter;

            switch (element.m_type) {
            case FormDataElement::data:
                if (!element.m_data.isEmpty()) {
                    // WebKit sometimes gives up empty data to append. These aren't
                    // necessary so we just optimize those out here.
                    base::Thread* thread = ioThread();
                    if (thread) {
                        Vector<char>* data = new Vector<char>(element.m_data);
                        thread->message_loop()->PostTask(FROM_HERE, NewRunnableMethod(m_request.get(), &WebRequest::appendBytesToUpload, data));
                    }
                }
                break;
            case FormDataElement::encodedFile:
                {
                    // Chromium check if it is a directory by checking
                    // element.m_fileLength, that doesn't work in Android
                    std::string filename = element.m_filename.utf8().data();
                    if (filename.size()) {
                        // Change from a url string to a filename
                        if (filename.find("file://") == 0) // Found at pos 0
                            filename.erase(0, 7);
                        base::Thread* thread = ioThread();
                        if (thread)
                            thread->message_loop()->PostTask(FROM_HERE, NewRunnableMethod(m_request.get(), &WebRequest::appendFileToUpload, filename));
                    }
                }
                break;
#if ENABLE(BLOB)
            case FormDataElement::encodedBlob:
                LOG_ASSERT(false, "Unexpected use of FormDataElement::encodedBlob");
                break;
#endif // ENABLE(BLOB)
            default:
                LOG_ASSERT(false, "Unexpected default case in WebUrlLoaderClient.cpp");
                break;
            }
        }
    }
}

bool WebUrlLoaderClient::start(bool sync, WebRequestContext* context)
{
    base::Thread* thread = ioThread();
    if (!thread) {
        return false;
    }

    m_sync = sync;
    if (m_sync) {
        AutoLock autoLock(*syncLock());
        thread->message_loop()->PostTask(FROM_HERE, NewRunnableMethod(m_request.get(), &WebRequest::start, context));

        // Run callbacks until the queue is exhausted and m_finished is true.
        while(!m_finished) {
            while (!m_queue.empty()) {
                OwnPtr<Task> task(m_queue.front());
                m_queue.pop_front();
                task->Run();
            }
            if (m_queue.empty() && !m_finished) {
                syncCondition()->Wait();
            }
        }

        // This may be the last reference to us, so we may be deleted now.
        // Don't access any more member variables after releasing this reference.
        m_resourceHandle = 0;
    } else {
        // Asynchronous start.
        thread->message_loop()->PostTask(FROM_HERE, NewRunnableMethod(m_request.get(), &WebRequest::start, context));
    }
    return true;
}

void WebUrlLoaderClient::downloadFile()
{
    if (m_response) {
        std::string contentDisposition;
        m_response->getHeader("content-disposition", &contentDisposition);
        m_webFrame->downloadStart(m_request->getUrl(), m_request->getUserAgent(), contentDisposition, m_response->getMimeType(), m_response->getExpectedSize());
    } else {
        LOGE("Unexpected call to downloadFile() before didReceiveResponse(). URL: %s", m_request->getUrl().c_str());
        // TODO: Turn off asserts crashing before release
        // http://b/issue?id=2951985
        CRASH();
    }
}

void WebUrlLoaderClient::cancel()
{
    m_cancelling = true;

    base::Thread* thread = ioThread();
    if (thread)
        thread->message_loop()->PostTask(FROM_HERE, NewRunnableMethod(m_request.get(), &WebRequest::cancel));
}

void WebUrlLoaderClient::setAuth(const std::string& username, const std::string& password)
{
    base::Thread* thread = ioThread();
    if (!thread) {
        return;
    }
    string16 username16 = ASCIIToUTF16(username);
    string16 password16 = ASCIIToUTF16(password);
    thread->message_loop()->PostTask(FROM_HERE, NewRunnableMethod(m_request.get(), &WebRequest::setAuth, username16, password16));
}

void WebUrlLoaderClient::cancelAuth()
{
    base::Thread* thread = ioThread();
    if (!thread) {
        return;
    }
    thread->message_loop()->PostTask(FROM_HERE, NewRunnableMethod(m_request.get(), &WebRequest::cancelAuth));
}

void WebUrlLoaderClient::proceedSslCertError()
{
    base::Thread* thread = ioThread();
    if (!thread) {
        return;
    }
    thread->message_loop()->PostTask(FROM_HERE, NewRunnableMethod(m_request.get(), &WebRequest::proceedSslCertError));
}

void WebUrlLoaderClient::cancelSslCertError(int cert_error)
{
    base::Thread* thread = ioThread();
    if (!thread) {
        return;
    }
    thread->message_loop()->PostTask(FROM_HERE, NewRunnableMethod(m_request.get(), &WebRequest::cancelSslCertError, cert_error));
}


void WebUrlLoaderClient::finish()
{
    m_finished = true;
    if (!m_sync) {
        // This is the last reference to us, so we will be deleted now.
        // We only release the reference here if start() was called asynchronously!
        m_resourceHandle = 0;
    }
    m_request = 0;
}

namespace {
// Trampoline to wrap a Chromium Task* in a WebKit-style static function + void*.
static void RunTask(void* v) {
    OwnPtr<Task> task(static_cast<Task*>(v));
    task->Run();
}
}

// This is called from the IO thread, and dispatches the callback to the main thread.
void WebUrlLoaderClient::maybeCallOnMainThread(Task* task)
{
    if (m_sync) {
        AutoLock autoLock(*syncLock());
        if (m_queue.empty()) {
            syncCondition()->Broadcast();
        }
        m_queue.push_back(task);
    } else {
        // Let WebKit handle it.
        callOnMainThread(RunTask, task);
    }
}

// Response methods
void WebUrlLoaderClient::didReceiveResponse(PassOwnPtr<WebResponse> webResponse)
{
    if (!isActive())
        return;

    m_response = webResponse;
    m_resourceHandle->client()->didReceiveResponse(m_resourceHandle.get(), m_response->createResourceResponse());
}

void WebUrlLoaderClient::didReceiveData(scoped_refptr<net::IOBuffer> buf, int size)
{
    if (!isActive())
        return;

    // didReceiveData will take a copy of the data
    if (m_resourceHandle && m_resourceHandle->client())
        m_resourceHandle->client()->didReceiveData(m_resourceHandle.get(), buf->data(), size, size);
}

// For data url's
void WebUrlLoaderClient::didReceiveDataUrl(PassOwnPtr<std::string> str)
{
    if (!isActive())
        return;

    // didReceiveData will take a copy of the data
    m_resourceHandle->client()->didReceiveData(m_resourceHandle.get(), str->data(), str->size(), str->size());
}

// For special android files
void WebUrlLoaderClient::didReceiveAndroidFileData(PassOwnPtr<std::vector<char> > vector)
{
    if (!isActive())
        return;

    // didReceiveData will take a copy of the data
    m_resourceHandle->client()->didReceiveData(m_resourceHandle.get(), vector->begin(), vector->size(), vector->size());
}

void WebUrlLoaderClient::didFail(PassOwnPtr<WebResponse> webResponse)
{
    if (isActive())
        m_resourceHandle->client()->didFail(m_resourceHandle.get(), webResponse->createResourceError());

    // Always finish a request, if not it will leak
    finish();
}

void WebUrlLoaderClient::willSendRequest(PassOwnPtr<WebResponse> webResponse)
{
    if (!isActive())
        return;

    // FIXME: This implies that the original request was from a user gesture.
    // For now, this is probably ok as this is just here to get the auto-login
    // demo working.  b/3291580.
    WebCore::UserGestureIndicator gesture(WebCore::DefinitelyProcessingUserGesture);

    KURL url = webResponse->createKurl();
    OwnPtr<WebCore::ResourceRequest> resourceRequest(new WebCore::ResourceRequest(url));
    m_resourceHandle->client()->willSendRequest(m_resourceHandle.get(), *resourceRequest, webResponse->createResourceResponse());

    // WebKit may have killed the request.
    if (!isActive())
        return;

    // Like Chrome, we only follow the redirect if WebKit left the URL unmodified.
    if (url == resourceRequest->url()) {
        ioThread()->message_loop()->PostTask(FROM_HERE, NewRunnableMethod(m_request.get(), &WebRequest::followDeferredRedirect));
    } else {
        cancel();
    }
}

void WebUrlLoaderClient::didFinishLoading()
{
    if (isActive())
        m_resourceHandle->client()->didFinishLoading(m_resourceHandle.get(), 0);

    // Always finish a request, if not it will leak
    finish();
}

void WebUrlLoaderClient::authRequired(scoped_refptr<net::AuthChallengeInfo> authChallengeInfo, bool firstTime)
{
    if (!isActive()) {
        return;
    }

    std::string host = base::SysWideToUTF8(authChallengeInfo->host_and_port);
    std::string realm = base::SysWideToUTF8(authChallengeInfo->realm);

    m_webFrame->didReceiveAuthenticationChallenge(this, host, realm, firstTime);
}

void WebUrlLoaderClient::reportSslCertError(int cert_error, net::X509Certificate* cert)
{
    if (!isActive()) return;
    std::vector<std::string> chain_bytes;
    cert->GetChainDEREncodedBytes(&chain_bytes);
    m_webFrame->reportSslCertError(this, cert_error, chain_bytes[0]);
}

} // namespace android
