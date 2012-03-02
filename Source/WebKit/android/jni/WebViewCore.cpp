/*
 * Copyright 2006, The Android Open Source Project
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

#define LOG_TAG "webcoreglue"

#include "config.h"
#include "WebViewCore.h"

#include "AccessibilityObject.h"
#include "AndroidHitTestResult.h"
#include "Attribute.h"
#include "BaseLayerAndroid.h"
#include "content/address_detector.h"
#include "Chrome.h"
#include "ChromeClientAndroid.h"
#include "ChromiumIncludes.h"
#include "ClientRect.h"
#include "ClientRectList.h"
#include "Color.h"
#include "CSSPropertyNames.h"
#include "CSSValueKeywords.h"
#include "DatabaseTracker.h"
#include "Document.h"
#include "DocumentMarkerController.h"
#include "DOMWindow.h"
#include "DOMSelection.h"
#include "Element.h"
#include "Editor.h"
#include "EditorClientAndroid.h"
#include "EventHandler.h"
#include "EventNames.h"
#include "ExceptionCode.h"
#include "FocusController.h"
#include "Font.h"
#include "Frame.h"
#include "FrameLoader.h"
#include "FrameLoaderClientAndroid.h"
#include "FrameTree.h"
#include "FrameView.h"
#include "Geolocation.h"
#include "GraphicsContext.h"
#include "GraphicsJNI.h"
#include "HTMLAnchorElement.h"
#include "HTMLAreaElement.h"
#include "HTMLElement.h"
#include "HTMLFormControlElement.h"
#include "HTMLImageElement.h"
#include "HTMLInputElement.h"
#include "HTMLLabelElement.h"
#include "HTMLMapElement.h"
#include "HTMLNames.h"
#include "HTMLOptGroupElement.h"
#include "HTMLOptionElement.h"
#include "HTMLSelectElement.h"
#include "HTMLTextAreaElement.h"
#include "HistoryItem.h"
#include "HitTestRequest.h"
#include "HitTestResult.h"
#include "InlineTextBox.h"
#include "KeyboardEvent.h"
#include "MemoryUsage.h"
#include "NamedNodeMap.h"
#include "Navigator.h"
#include "Node.h"
#include "NodeList.h"
#include "Page.h"
#include "PageGroup.h"
#include "PlatformKeyboardEvent.h"
#include "PlatformString.h"
#include "PluginWidgetAndroid.h"
#include "PluginView.h"
#include "Position.h"
#include "ProgressTracker.h"
#include "Range.h"
#include "RenderBox.h"
#include "RenderImage.h"
#include "RenderInline.h"
#include "RenderLayer.h"
#include "RenderPart.h"
#include "RenderText.h"
#include "RenderTextControl.h"
#include "RenderThemeAndroid.h"
#include "RenderView.h"
#include "ResourceRequest.h"
#include "RuntimeEnabledFeatures.h"
#include "SchemeRegistry.h"
#include "ScriptController.h"
#include "SelectionController.h"
#include "SelectText.h"
#include "Settings.h"
#include "SkANP.h"
#include "SkTemplates.h"
#include "SkTDArray.h"
#include "SkTypes.h"
#include "SkCanvas.h"
#include "SkPicture.h"
#include "SkUtils.h"
#include "Text.h"
#include "TextIterator.h"
#include "TypingCommand.h"
#include "WebCache.h"
#include "WebCoreFrameBridge.h"
#include "WebFrameView.h"
#include "WindowsKeyboardCodes.h"
#include "android_graphics.h"
#include "autofill/WebAutofill.h"
#include "htmlediting.h"
#include "markup.h"
#include "visible_units.h"

#include <JNIHelp.h>
#include <JNIUtility.h>
#include <androidfw/KeycodeLabels.h>
#include <v8.h>
#include <wtf/CurrentTime.h>
#include <wtf/text/AtomicString.h>
#include <wtf/text/CString.h>
#include <wtf/text/StringImpl.h>

#if DEBUG_NAV_UI
#include "SkTime.h"
#endif

#if ENABLE(TOUCH_EVENTS) // Android
#include "PlatformTouchEvent.h"
#endif

#ifdef ANDROID_DOM_LOGGING
#include "AndroidLog.h"
#include "RenderTreeAsText.h"
#include <wtf/text/CString.h>

FILE* gDomTreeFile = 0;
FILE* gRenderTreeFile = 0;
#endif

#if USE(ACCELERATED_COMPOSITING)
#include "GraphicsLayerAndroid.h"
#include "RenderLayerCompositor.h"
#endif

// In some cases, too many invalidations passed to the UI will slow us down.
// Limit ourselves to 32 rectangles, past this just send the area bounds to the UI.
// see WebViewCore::recordPictureSet().
#define MAX_INVALIDATIONS 32

/*  We pass this flag when recording the actual content, so that we don't spend
    time actually regionizing complex path clips, when all we really want to do
    is record them.
 */
#define PICT_RECORD_FLAGS   SkPicture::kUsePathBoundsForClip_RecordingFlag

////////////////////////////////////////////////////////////////////////////////////////////////

namespace android {

// Copied from CacheBuilder, not sure if this is needed/correct
IntRect getAreaRect(const HTMLAreaElement* area)
{
    Node* node = area->document();
    while ((node = node->traverseNextNode()) != NULL) {
        RenderObject* renderer = node->renderer();
        if (renderer && renderer->isRenderImage()) {
            RenderImage* image = static_cast<RenderImage*>(renderer);
            HTMLMapElement* map = image->imageMap();
            if (map) {
                Node* n;
                for (n = map->firstChild(); n;
                        n = n->traverseNextNode(map)) {
                    if (n == area) {
                        if (area->isDefault())
                            return image->absoluteBoundingBoxRect();
                        return area->computeRect(image);
                    }
                }
            }
        }
    }
    return IntRect();
}

// Copied from CacheBuilder, not sure if this is needed/correct
// TODO: See if this is even needed (I suspect not), and if not remove it
bool validNode(Frame* startFrame, void* matchFrame,
        void* matchNode)
{
    if (matchFrame == startFrame) {
        if (matchNode == NULL)
            return true;
        Node* node = startFrame->document();
        while (node != NULL) {
            if (node == matchNode) {
                const IntRect& rect = node->hasTagName(HTMLNames::areaTag) ?
                    getAreaRect(static_cast<HTMLAreaElement*>(node)) : node->getRect();
                // Consider nodes with empty rects that are not at the origin
                // to be valid, since news.google.com has valid nodes like this
                if (rect.x() == 0 && rect.y() == 0 && rect.isEmpty())
                    return false;
                return true;
            }
            node = node->traverseNextNode();
        }
        return false;
    }
    Frame* child = startFrame->tree()->firstChild();
    while (child) {
        bool result = validNode(child, matchFrame, matchNode);
        if (result)
            return result;
        child = child->tree()->nextSibling();
    }
    return false;
}

static SkTDArray<WebViewCore*> gInstanceList;

void WebViewCore::addInstance(WebViewCore* inst) {
    *gInstanceList.append() = inst;
}

void WebViewCore::removeInstance(WebViewCore* inst) {
    int index = gInstanceList.find(inst);
    ALOG_ASSERT(index >= 0, "RemoveInstance inst not found");
    if (index >= 0) {
        gInstanceList.removeShuffle(index);
    }
}

bool WebViewCore::isInstance(WebViewCore* inst) {
    return gInstanceList.find(inst) >= 0;
}

jobject WebViewCore::getApplicationContext() {

    // check to see if there is a valid webviewcore object
    if (gInstanceList.isEmpty())
        return 0;

    // get the context from the webview
    jobject context = gInstanceList[0]->getContext();

    if (!context)
        return 0;

    // get the application context using JNI
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jclass contextClass = env->GetObjectClass(context);
    jmethodID appContextMethod = env->GetMethodID(contextClass, "getApplicationContext", "()Landroid/content/Context;");
    env->DeleteLocalRef(contextClass);
    jobject result = env->CallObjectMethod(context, appContextMethod);
    checkException(env);
    return result;
}


struct WebViewCoreStaticMethods {
    jmethodID    m_isSupportedMediaMimeType;
} gWebViewCoreStaticMethods;

// Check whether a media mimeType is supported in Android media framework.
bool WebViewCore::isSupportedMediaMimeType(const WTF::String& mimeType) {
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jstring jMimeType = wtfStringToJstring(env, mimeType);
    jclass webViewCore = env->FindClass("android/webkit/WebViewCore");
    bool val = env->CallStaticBooleanMethod(webViewCore,
          gWebViewCoreStaticMethods.m_isSupportedMediaMimeType, jMimeType);
    checkException(env);
    env->DeleteLocalRef(webViewCore);
    env->DeleteLocalRef(jMimeType);

    return val;
}

// ----------------------------------------------------------------------------

// Field ids for WebViewCore
struct WebViewCoreFields {
    jfieldID    m_nativeClass;
    jfieldID    m_viewportWidth;
    jfieldID    m_viewportHeight;
    jfieldID    m_viewportInitialScale;
    jfieldID    m_viewportMinimumScale;
    jfieldID    m_viewportMaximumScale;
    jfieldID    m_viewportUserScalable;
    jfieldID    m_viewportDensityDpi;
    jfieldID    m_webView;
    jfieldID    m_drawIsPaused;
    jfieldID    m_lowMemoryUsageMb;
    jfieldID    m_highMemoryUsageMb;
    jfieldID    m_highUsageDeltaMb;
} gWebViewCoreFields;

// ----------------------------------------------------------------------------

struct WebViewCore::JavaGlue {
    jweak       m_obj;
    jmethodID   m_scrollTo;
    jmethodID   m_contentDraw;
    jmethodID   m_layersDraw;
    jmethodID   m_requestListBox;
    jmethodID   m_openFileChooser;
    jmethodID   m_requestSingleListBox;
    jmethodID   m_jsAlert;
    jmethodID   m_jsConfirm;
    jmethodID   m_jsPrompt;
    jmethodID   m_jsUnload;
    jmethodID   m_jsInterrupt;
    jmethodID   m_didFirstLayout;
    jmethodID   m_updateViewport;
    jmethodID   m_sendNotifyProgressFinished;
    jmethodID   m_sendViewInvalidate;
    jmethodID   m_updateTextfield;
    jmethodID   m_updateTextSelection;
    jmethodID   m_clearTextEntry;
    jmethodID   m_restoreScale;
    jmethodID   m_needTouchEvents;
    jmethodID   m_requestKeyboard;
    jmethodID   m_exceededDatabaseQuota;
    jmethodID   m_reachedMaxAppCacheSize;
    jmethodID   m_populateVisitedLinks;
    jmethodID   m_geolocationPermissionsShowPrompt;
    jmethodID   m_geolocationPermissionsHidePrompt;
    jmethodID   m_getDeviceMotionService;
    jmethodID   m_getDeviceOrientationService;
    jmethodID   m_addMessageToConsole;
    jmethodID   m_formDidBlur;
    jmethodID   m_focusNodeChanged;
    jmethodID   m_getPluginClass;
    jmethodID   m_showFullScreenPlugin;
    jmethodID   m_hideFullScreenPlugin;
    jmethodID   m_createSurface;
    jmethodID   m_addSurface;
    jmethodID   m_updateSurface;
    jmethodID   m_destroySurface;
    jmethodID   m_getContext;
    jmethodID   m_keepScreenOn;
    jmethodID   m_showRect;
    jmethodID   m_centerFitRect;
    jmethodID   m_setScrollbarModes;
    jmethodID   m_setInstallableWebApp;
    jmethodID   m_enterFullscreenForVideoLayer;
    jmethodID   m_exitFullscreenVideo;
    jmethodID   m_setWebTextViewAutoFillable;
    jmethodID   m_selectAt;
    jmethodID   m_initEditField;
    jmethodID   m_updateMatchCount;
    AutoJObject object(JNIEnv* env) {
        // We hold a weak reference to the Java WebViewCore to avoid memeory
        // leaks due to circular references when WebView.destroy() is not
        // called manually. The WebView and hence the WebViewCore could become
        // weakly reachable at any time, after which the GC could null our weak
        // reference, so we have to check the return value of this method at
        // every use. Note that our weak reference will be nulled before the
        // WebViewCore is finalized.
        return getRealObject(env, m_obj);
    }
};

/*
 * WebViewCore Implementation
 */

static jmethodID GetJMethod(JNIEnv* env, jclass clazz, const char name[], const char signature[])
{
    jmethodID m = env->GetMethodID(clazz, name, signature);
    ALOG_ASSERT(m, "Could not find method %s", name);
    return m;
}

WebViewCore::WebViewCore(JNIEnv* env, jobject javaWebViewCore, WebCore::Frame* mainframe)
    : m_moveGeneration(0)
    , m_touchGeneration(0)
    , m_lastGeneration(0)
    , m_findIsUp(false)
    , m_javaGlue(new JavaGlue)
    , m_mainFrame(mainframe)
    , m_popupReply(0)
    , m_lastFocused(0)
    , m_lastFocusedBounds(WebCore::IntRect(0,0,0,0))
    , m_blurringNodePointer(0)
    , m_lastFocusedSelStart(0)
    , m_lastFocusedSelEnd(0)
    , m_blockTextfieldUpdates(false)
    , m_focusBoundsChanged(false)
    , m_skipContentDraw(false)
    , m_textGeneration(0)
    , m_maxXScroll(320/4)
    , m_maxYScroll(240/4)
    , m_scrollOffsetX(0)
    , m_scrollOffsetY(0)
    , m_mousePos(WebCore::IntPoint(0,0))
    , m_frameCacheOutOfDate(true)
    , m_progressDone(false)
    , m_screenWidth(320)
    , m_screenHeight(240)
    , m_textWrapWidth(320)
    , m_scale(1.0f)
    , m_domtree_version(0)
    , m_check_domtree_version(true)
    , m_groupForVisitedLinks(0)
    , m_isPaused(false)
    , m_cacheMode(0)
    , m_fullscreenVideoMode(false)
    , m_matchCount(0)
    , m_activeMatchIndex(0)
    , m_activeMatch(0)
    , m_pluginInvalTimer(this, &WebViewCore::pluginInvalTimerFired)
    , m_screenOnCounter(0)
    , m_currentNodeDomNavigationAxis(0)
    , m_deviceMotionAndOrientationManager(this)
#if ENABLE(TOUCH_EVENTS)
    , m_forwardingTouchEvents(false)
#endif
    , m_webRequestContext(0)
{
    ALOG_ASSERT(m_mainFrame, "Uh oh, somehow a frameview was made without an initial frame!");

    jclass clazz = env->GetObjectClass(javaWebViewCore);
    m_javaGlue->m_obj = env->NewWeakGlobalRef(javaWebViewCore);
    m_javaGlue->m_scrollTo = GetJMethod(env, clazz, "contentScrollTo", "(IIZZ)V");
    m_javaGlue->m_contentDraw = GetJMethod(env, clazz, "contentDraw", "()V");
    m_javaGlue->m_layersDraw = GetJMethod(env, clazz, "layersDraw", "()V");
    m_javaGlue->m_requestListBox = GetJMethod(env, clazz, "requestListBox", "([Ljava/lang/String;[I[I)V");
    m_javaGlue->m_openFileChooser = GetJMethod(env, clazz, "openFileChooser", "(Ljava/lang/String;)Ljava/lang/String;");
    m_javaGlue->m_requestSingleListBox = GetJMethod(env, clazz, "requestListBox", "([Ljava/lang/String;[II)V");
    m_javaGlue->m_jsAlert = GetJMethod(env, clazz, "jsAlert", "(Ljava/lang/String;Ljava/lang/String;)V");
    m_javaGlue->m_jsConfirm = GetJMethod(env, clazz, "jsConfirm", "(Ljava/lang/String;Ljava/lang/String;)Z");
    m_javaGlue->m_jsPrompt = GetJMethod(env, clazz, "jsPrompt", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    m_javaGlue->m_jsUnload = GetJMethod(env, clazz, "jsUnload", "(Ljava/lang/String;Ljava/lang/String;)Z");
    m_javaGlue->m_jsInterrupt = GetJMethod(env, clazz, "jsInterrupt", "()Z");
    m_javaGlue->m_didFirstLayout = GetJMethod(env, clazz, "didFirstLayout", "(Z)V");
    m_javaGlue->m_updateViewport = GetJMethod(env, clazz, "updateViewport", "()V");
    m_javaGlue->m_sendNotifyProgressFinished = GetJMethod(env, clazz, "sendNotifyProgressFinished", "()V");
    m_javaGlue->m_sendViewInvalidate = GetJMethod(env, clazz, "sendViewInvalidate", "(IIII)V");
    m_javaGlue->m_updateTextfield = GetJMethod(env, clazz, "updateTextfield", "(IZLjava/lang/String;I)V");
    m_javaGlue->m_updateTextSelection = GetJMethod(env, clazz, "updateTextSelection", "(IIIII)V");
    m_javaGlue->m_clearTextEntry = GetJMethod(env, clazz, "clearTextEntry", "()V");
    m_javaGlue->m_restoreScale = GetJMethod(env, clazz, "restoreScale", "(FF)V");
    m_javaGlue->m_needTouchEvents = GetJMethod(env, clazz, "needTouchEvents", "(Z)V");
    m_javaGlue->m_requestKeyboard = GetJMethod(env, clazz, "requestKeyboard", "(Z)V");
    m_javaGlue->m_exceededDatabaseQuota = GetJMethod(env, clazz, "exceededDatabaseQuota", "(Ljava/lang/String;Ljava/lang/String;JJ)V");
    m_javaGlue->m_reachedMaxAppCacheSize = GetJMethod(env, clazz, "reachedMaxAppCacheSize", "(J)V");
    m_javaGlue->m_populateVisitedLinks = GetJMethod(env, clazz, "populateVisitedLinks", "()V");
    m_javaGlue->m_geolocationPermissionsShowPrompt = GetJMethod(env, clazz, "geolocationPermissionsShowPrompt", "(Ljava/lang/String;)V");
    m_javaGlue->m_geolocationPermissionsHidePrompt = GetJMethod(env, clazz, "geolocationPermissionsHidePrompt", "()V");
    m_javaGlue->m_getDeviceMotionService = GetJMethod(env, clazz, "getDeviceMotionService", "()Landroid/webkit/DeviceMotionService;");
    m_javaGlue->m_getDeviceOrientationService = GetJMethod(env, clazz, "getDeviceOrientationService", "()Landroid/webkit/DeviceOrientationService;");
    m_javaGlue->m_addMessageToConsole = GetJMethod(env, clazz, "addMessageToConsole", "(Ljava/lang/String;ILjava/lang/String;I)V");
    m_javaGlue->m_formDidBlur = GetJMethod(env, clazz, "formDidBlur", "(I)V");
    m_javaGlue->m_focusNodeChanged = GetJMethod(env, clazz, "focusNodeChanged", "(Landroid/webkit/WebViewCore$WebKitHitTest;)V");
    m_javaGlue->m_getPluginClass = GetJMethod(env, clazz, "getPluginClass", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/Class;");
    m_javaGlue->m_showFullScreenPlugin = GetJMethod(env, clazz, "showFullScreenPlugin", "(Landroid/webkit/ViewManager$ChildView;II)V");
    m_javaGlue->m_hideFullScreenPlugin = GetJMethod(env, clazz, "hideFullScreenPlugin", "()V");
    m_javaGlue->m_createSurface = GetJMethod(env, clazz, "createSurface", "(Landroid/view/View;)Landroid/webkit/ViewManager$ChildView;");
    m_javaGlue->m_addSurface = GetJMethod(env, clazz, "addSurface", "(Landroid/view/View;IIII)Landroid/webkit/ViewManager$ChildView;");
    m_javaGlue->m_updateSurface = GetJMethod(env, clazz, "updateSurface", "(Landroid/webkit/ViewManager$ChildView;IIII)V");
    m_javaGlue->m_destroySurface = GetJMethod(env, clazz, "destroySurface", "(Landroid/webkit/ViewManager$ChildView;)V");
    m_javaGlue->m_getContext = GetJMethod(env, clazz, "getContext", "()Landroid/content/Context;");
    m_javaGlue->m_keepScreenOn = GetJMethod(env, clazz, "keepScreenOn", "(Z)V");
    m_javaGlue->m_showRect = GetJMethod(env, clazz, "showRect", "(IIIIIIFFFF)V");
    m_javaGlue->m_centerFitRect = GetJMethod(env, clazz, "centerFitRect", "(IIII)V");
    m_javaGlue->m_setScrollbarModes = GetJMethod(env, clazz, "setScrollbarModes", "(II)V");
    m_javaGlue->m_setInstallableWebApp = GetJMethod(env, clazz, "setInstallableWebApp", "()V");
#if ENABLE(VIDEO)
    m_javaGlue->m_enterFullscreenForVideoLayer = GetJMethod(env, clazz, "enterFullscreenForVideoLayer", "(ILjava/lang/String;)V");
    m_javaGlue->m_exitFullscreenVideo = GetJMethod(env, clazz, "exitFullscreenVideo", "()V");
#endif
    m_javaGlue->m_setWebTextViewAutoFillable = GetJMethod(env, clazz, "setWebTextViewAutoFillable", "(ILjava/lang/String;)V");
    m_javaGlue->m_selectAt = GetJMethod(env, clazz, "selectAt", "(II)V");
    m_javaGlue->m_initEditField = GetJMethod(env, clazz, "initEditField", "(ILjava/lang/String;IZZLjava/lang/String;IIII)V");
    m_javaGlue->m_updateMatchCount = GetJMethod(env, clazz, "updateMatchCount", "(IILjava/lang/String;)V");
    env->DeleteLocalRef(clazz);

    env->SetIntField(javaWebViewCore, gWebViewCoreFields.m_nativeClass, (jint)this);

    PageGroup::setShouldTrackVisitedLinks(true);

    clearContent();

    MemoryUsage::setLowMemoryUsageMb(env->GetIntField(javaWebViewCore, gWebViewCoreFields.m_lowMemoryUsageMb));
    MemoryUsage::setHighMemoryUsageMb(env->GetIntField(javaWebViewCore, gWebViewCoreFields.m_highMemoryUsageMb));
    MemoryUsage::setHighUsageDeltaMb(env->GetIntField(javaWebViewCore, gWebViewCoreFields.m_highUsageDeltaMb));

    WebViewCore::addInstance(this);

    AndroidNetworkLibraryImpl::InitWithApplicationContext(env, 0);

    // Static initialisation of certain important V8 static data gets performed at system startup when
    // libwebcore gets loaded. We now need to associate the WebCore thread with V8 to complete
    // initialisation.
    v8::V8::Initialize();

    // Configure any RuntimeEnabled features that we need to change from their default now.
    // See WebCore/bindings/generic/RuntimeEnabledFeatures.h

    // HTML5 History API
    RuntimeEnabledFeatures::setPushStateEnabled(true);
}

WebViewCore::~WebViewCore()
{
    WebViewCore::removeInstance(this);

    // Release the focused view
    Release(m_popupReply);

    if (m_javaGlue->m_obj) {
        JNIEnv* env = JSC::Bindings::getJNIEnv();
        env->DeleteWeakGlobalRef(m_javaGlue->m_obj);
        m_javaGlue->m_obj = 0;
    }
    delete m_javaGlue;
}

WebViewCore* WebViewCore::getWebViewCore(const WebCore::FrameView* view)
{
    if (!view)
        return 0;
    if (view->platformWidget())
        return static_cast<WebFrameView*>(view->platformWidget())->webViewCore();
    Frame* frame = view->frame();
    while (Frame* parent = frame->tree()->parent())
        frame = parent;
    WebFrameView* webFrameView = 0;
    if (frame && frame->view())
        webFrameView = static_cast<WebFrameView*>(frame->view()->platformWidget());
    if (!webFrameView)
        return 0;
    return webFrameView->webViewCore();
}

WebViewCore* WebViewCore::getWebViewCore(const WebCore::ScrollView* view)
{
    if (!view)
        return 0;
    if (view->platformWidget())
        return static_cast<WebFrameView*>(view->platformWidget())->webViewCore();
    const FrameView* frameView = 0;
    if (view->isFrameView())
        frameView = static_cast<const FrameView*>(view);
    else {
        frameView = static_cast<const FrameView*>(view->root());
        if (!frameView)
            return 0;
    }
    return getWebViewCore(frameView);
}

static bool layoutIfNeededRecursive(WebCore::Frame* f)
{
    if (!f)
        return true;

    WebCore::FrameView* v = f->view();
    if (!v)
        return true;

    if (v->needsLayout())
        v->layout(f->tree()->parent());

    WebCore::Frame* child = f->tree()->firstChild();
    bool success = true;
    while (child) {
        success &= layoutIfNeededRecursive(child);
        child = child->tree()->nextSibling();
    }

    return success && !v->needsLayout();
}

WebCore::Node* WebViewCore::currentFocus()
{
    return focusedFrame()->document()->focusedNode();
}

void WebViewCore::recordPictureSet(PictureSet* content)
{
    // if there is no document yet, just return
    if (!m_mainFrame->document()) {
        DBG_SET_LOG("!m_mainFrame->document()");
        return;
    }
    if (m_addInval.isEmpty()) {
        DBG_SET_LOG("m_addInval.isEmpty()");
        return;
    }
    // Call layout to ensure that the contentWidth and contentHeight are correct
    // it's fine for layout to gather invalidates, but defeat sending a message
    // back to java to call webkitDraw, since we're already in the middle of
    // doing that
    m_skipContentDraw = true;
    bool success = layoutIfNeededRecursive(m_mainFrame);
    m_skipContentDraw = false;

    // We may be mid-layout and thus cannot draw.
    if (!success)
        return;

    // if the webkit page dimensions changed, discard the pictureset and redraw.
    WebCore::FrameView* view = m_mainFrame->view();
    int width = view->contentsWidth();
    int height = view->contentsHeight();

    // Use the contents width and height as a starting point.
    SkIRect contentRect;
    contentRect.set(0, 0, width, height);
    SkIRect total(contentRect);

    // Traverse all the frames and add their sizes if they are in the visible
    // rectangle.
    for (WebCore::Frame* frame = m_mainFrame->tree()->traverseNext(); frame;
            frame = frame->tree()->traverseNext()) {
        // If the frame doesn't have an owner then it is the top frame and the
        // view size is the frame size.
        WebCore::RenderPart* owner = frame->ownerRenderer();
        if (owner && owner->style()->visibility() == VISIBLE) {
            int x = owner->x();
            int y = owner->y();

            // Traverse the tree up to the parent to find the absolute position
            // of this frame.
            WebCore::Frame* parent = frame->tree()->parent();
            while (parent) {
                WebCore::RenderPart* parentOwner = parent->ownerRenderer();
                if (parentOwner) {
                    x += parentOwner->x();
                    y += parentOwner->y();
                }
                parent = parent->tree()->parent();
            }
            // Use the owner dimensions so that padding and border are
            // included.
            int right = x + owner->width();
            int bottom = y + owner->height();
            SkIRect frameRect = {x, y, right, bottom};
            // Ignore a width or height that is smaller than 1. Some iframes
            // have small dimensions in order to be hidden. The iframe
            // expansion code does not expand in that case so we should ignore
            // them here.
            if (frameRect.width() > 1 && frameRect.height() > 1
                    && SkIRect::Intersects(total, frameRect))
                total.join(x, y, right, bottom);
        }
    }

    // If the new total is larger than the content, resize the view to include
    // all the content.
    if (!contentRect.contains(total)) {
        // Resize the view to change the overflow clip.
        view->resize(total.fRight, total.fBottom);

        // We have to force a layout in order for the clip to change.
        m_mainFrame->contentRenderer()->setNeedsLayoutAndPrefWidthsRecalc();
        view->forceLayout();

        // Relayout similar to above
        m_skipContentDraw = true;
        bool success = layoutIfNeededRecursive(m_mainFrame);
        m_skipContentDraw = false;
        if (!success)
            return;

        // Set the computed content width
        width = view->contentsWidth();
        height = view->contentsHeight();
    }

#if USE(ACCELERATED_COMPOSITING)
    // The invals are not always correct when the content size has changed. For
    // now, let's just reset the inval so that it invalidates the entire content
    // -- the pictureset will be fully repainted, tiles will be marked dirty and
    // will have to be repainted.

    // FIXME: the webkit invals ought to have been enough...
    if (content->width() != width || content->height() != height) {
        SkIRect r;
        r.fLeft = 0;
        r.fTop = 0;
        r.fRight = width;
        r.fBottom = height;
        m_addInval.setRect(r);
    }
#endif

    content->setDimensions(width, height, &m_addInval);

    // Add the current inval rects to the PictureSet, and rebuild it.
    content->add(m_addInval, 0, false);

    // If we have too many invalidations, just get the area bounds
    SkRegion::Iterator iterator(m_addInval);
    int nbInvals = 0;
    while (!iterator.done()) {
        iterator.next();
        nbInvals++;
        if (nbInvals > MAX_INVALIDATIONS)
            break;
    }
    if (nbInvals > MAX_INVALIDATIONS) {
        SkIRect r = m_addInval.getBounds();
        m_addInval.setRect(r);
    }
    // Rebuild the pictureset (webkit repaint)
    rebuildPictureSet(content);
}

void WebViewCore::clearContent()
{
    DBG_SET_LOG("");
    m_content.clear();
    m_addInval.setEmpty();
    m_rebuildInval.setEmpty();
}

bool WebViewCore::focusBoundsChanged()
{
    bool result = m_focusBoundsChanged;
    m_focusBoundsChanged = false;
    return result;
}

SkPicture* WebViewCore::rebuildPicture(const SkIRect& inval)
{
    WebCore::FrameView* view = m_mainFrame->view();
    int width = view->contentsWidth();
    int height = view->contentsHeight();
    SkPicture* picture = new SkPicture();
    SkAutoPictureRecord arp(picture, width, height, PICT_RECORD_FLAGS);
    SkAutoMemoryUsageProbe mup(__FUNCTION__);
    SkCanvas* recordingCanvas = arp.getRecordingCanvas();

    WebCore::PlatformGraphicsContext pgc(recordingCanvas);
    WebCore::GraphicsContext gc(&pgc);
    IntPoint origin = view->minimumScrollPosition();
    WebCore::IntRect drawArea(inval.fLeft + origin.x(), inval.fTop + origin.y(),
            inval.width(), inval.height());
    recordingCanvas->translate(-drawArea.x(), -drawArea.y());
    recordingCanvas->save();
    view->platformWidget()->draw(&gc, drawArea);
    m_rebuildInval.op(inval, SkRegion::kUnion_Op);
    DBG_SET_LOGD("m_rebuildInval={%d,%d,r=%d,b=%d}",
        m_rebuildInval.getBounds().fLeft, m_rebuildInval.getBounds().fTop,
        m_rebuildInval.getBounds().fRight, m_rebuildInval.getBounds().fBottom);

    return picture;
}

void WebViewCore::rebuildPictureSet(PictureSet* pictureSet)
{
#ifdef FAST_PICTURESET
    WTF::Vector<Bucket*>* buckets = pictureSet->bucketsToUpdate();

    for (unsigned int i = 0; i < buckets->size(); i++) {
        Bucket* bucket = (*buckets)[i];
        for (unsigned int j = 0; j < bucket->size(); j++) {
            BucketPicture& bucketPicture = (*bucket)[j];
            const SkIRect& inval = bucketPicture.mRealArea;
            SkPicture* picture = rebuildPicture(inval);
            SkSafeUnref(bucketPicture.mPicture);
            bucketPicture.mPicture = picture;
        }
    }
    buckets->clear();
#else
    size_t size = pictureSet->size();
    for (size_t index = 0; index < size; index++) {
        if (pictureSet->upToDate(index))
            continue;
        const SkIRect& inval = pictureSet->bounds(index);
        DBG_SET_LOGD("pictSet=%p [%d] {%d,%d,w=%d,h=%d}", pictureSet, index,
            inval.fLeft, inval.fTop, inval.width(), inval.height());
        pictureSet->setPicture(index, rebuildPicture(inval));
    }

    pictureSet->validate(__FUNCTION__);
#endif
}

bool WebViewCore::updateLayers(LayerAndroid* layers)
{
    // We update the layers
    ChromeClientAndroid* chromeC = static_cast<ChromeClientAndroid*>(m_mainFrame->page()->chrome()->client());
    GraphicsLayerAndroid* root = static_cast<GraphicsLayerAndroid*>(chromeC->layersSync());
    if (root) {
        LayerAndroid* updatedLayer = root->contentLayer();
        return layers->updateWithTree(updatedLayer);
    }
    return true;
}

void WebViewCore::notifyAnimationStarted()
{
    // We notify webkit that the animations have begun
    // TODO: handle case where not all have begun
    ChromeClientAndroid* chromeC = static_cast<ChromeClientAndroid*>(m_mainFrame->page()->chrome()->client());
    GraphicsLayerAndroid* root = static_cast<GraphicsLayerAndroid*>(chromeC->layersSync());
    if (root)
        root->notifyClientAnimationStarted();

}

BaseLayerAndroid* WebViewCore::createBaseLayer(SkRegion* region)
{
    BaseLayerAndroid* base = new BaseLayerAndroid();
    base->setContent(m_content);

    m_skipContentDraw = true;
    bool layoutSucceeded = layoutIfNeededRecursive(m_mainFrame);
    m_skipContentDraw = false;
    // Layout only fails if called during a layout.
    ALOG_ASSERT(layoutSucceeded, "Can never be called recursively");

#if USE(ACCELERATED_COMPOSITING)
    // We set the background color
    if (m_mainFrame && m_mainFrame->document()
        && m_mainFrame->document()->body()) {
        Document* document = m_mainFrame->document();
        RefPtr<RenderStyle> style = document->styleForElementIgnoringPendingStylesheets(document->body());
        if (style->hasBackground()) {
            Color color = style->visitedDependentColor(CSSPropertyBackgroundColor);
            if (color.isValid() && color.alpha() > 0)
                base->setBackgroundColor(color);
        }
    }

    // We update the layers
    ChromeClientAndroid* chromeC = static_cast<ChromeClientAndroid*>(m_mainFrame->page()->chrome()->client());
    GraphicsLayerAndroid* root = static_cast<GraphicsLayerAndroid*>(chromeC->layersSync());
    if (root) {
        LayerAndroid* copyLayer = new LayerAndroid(*root->contentLayer());
        base->addChild(copyLayer);
        copyLayer->unref();
        root->contentLayer()->clearDirtyRegion();
    }
#endif

    return base;
}

BaseLayerAndroid* WebViewCore::recordContent(SkRegion* region, SkIPoint* point)
{
    DBG_SET_LOG("start");
    // If there is a pending style recalculation, just return.
    if (m_mainFrame->document()->isPendingStyleRecalc()) {
        DBG_SET_LOG("recordContent: pending style recalc, ignoring.");
        return 0;
    }
    float progress = (float) m_mainFrame->page()->progress()->estimatedProgress();
    m_progressDone = progress <= 0.0f || progress >= 1.0f;
    recordPictureSet(&m_content);
    if (!m_progressDone && m_content.isEmpty()) {
        DBG_SET_LOGD("empty (progress=%g)", progress);
        return 0;
    }
    region->set(m_addInval);
    m_addInval.setEmpty();
#if USE(ACCELERATED_COMPOSITING)
#else
    region->op(m_rebuildInval, SkRegion::kUnion_Op);
#endif
    m_rebuildInval.setEmpty();
    point->fX = m_content.width();
    point->fY = m_content.height();
    DBG_SET_LOGD("region={%d,%d,r=%d,b=%d}", region->getBounds().fLeft,
        region->getBounds().fTop, region->getBounds().fRight,
        region->getBounds().fBottom);
    DBG_SET_LOG("end");

    return createBaseLayer(region);
}

void WebViewCore::splitContent(PictureSet* content)
{
#ifdef FAST_PICTURESET
#else
    bool layoutSucceeded = layoutIfNeededRecursive(m_mainFrame);
    ALOG_ASSERT(layoutSucceeded, "Can never be called recursively");
    content->split(&m_content);
    rebuildPictureSet(&m_content);
    content->set(m_content);
#endif // FAST_PICTURESET
}

void WebViewCore::scrollTo(int x, int y, bool animate)
{
    ALOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_scrollTo,
            x, y, animate, false);
    checkException(env);
}

void WebViewCore::sendNotifyProgressFinished()
{
    ALOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_sendNotifyProgressFinished);
    checkException(env);
}

void WebViewCore::viewInvalidate(const WebCore::IntRect& rect)
{
    ALOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(),
                        m_javaGlue->m_sendViewInvalidate,
                        rect.x(), rect.y(), rect.maxX(), rect.maxY());
    checkException(env);
}

void WebViewCore::contentDraw()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_contentDraw);
    checkException(env);
}

void WebViewCore::layersDraw()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_layersDraw);
    checkException(env);
}

void WebViewCore::contentInvalidate(const WebCore::IntRect &r)
{
    DBG_SET_LOGD("rect={%d,%d,w=%d,h=%d}", r.x(), r.y(), r.width(), r.height());
    SkIRect rect(r);
    if (!rect.intersect(0, 0, INT_MAX, INT_MAX))
        return;
    m_addInval.op(rect, SkRegion::kUnion_Op);
    DBG_SET_LOGD("m_addInval={%d,%d,r=%d,b=%d}",
        m_addInval.getBounds().fLeft, m_addInval.getBounds().fTop,
        m_addInval.getBounds().fRight, m_addInval.getBounds().fBottom);
    if (!m_skipContentDraw)
        contentDraw();
}

void WebViewCore::contentInvalidateAll()
{
    WebCore::FrameView* view = m_mainFrame->view();
    contentInvalidate(WebCore::IntRect(0, 0,
        view->contentsWidth(), view->contentsHeight()));
}

void WebViewCore::offInvalidate(const WebCore::IntRect &r)
{
    // FIXME: these invalidates are offscreen, and can be throttled or
    // deferred until the area is visible. For now, treat them as
    // regular invals so that drawing happens (inefficiently) for now.
    contentInvalidate(r);
}

void WebViewCore::didFirstLayout()
{
    ALOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;

    const WebCore::KURL& url = m_mainFrame->document()->url();
    if (url.isEmpty())
        return;
    ALOGV("::WebCore:: didFirstLayout %s", url.string().ascii().data());

    WebCore::FrameLoadType loadType = m_mainFrame->loader()->loadType();

    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_didFirstLayout,
            loadType == WebCore::FrameLoadTypeStandard
            // When redirect with locked history, we would like to reset the
            // scale factor. This is important for www.yahoo.com as it is
            // redirected to www.yahoo.com/?rs=1 on load.
            || loadType == WebCore::FrameLoadTypeRedirectWithLockedBackForwardList
            // When "request desktop page" is used, we want to treat it as
            // a newly-loaded page.
            || loadType == WebCore::FrameLoadTypeSame);
    checkException(env);
}

void WebViewCore::updateViewport()
{
    ALOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_updateViewport);
    checkException(env);
}

void WebViewCore::restoreScale(float scale, float textWrapScale)
{
    ALOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_restoreScale, scale, textWrapScale);
    checkException(env);
}

void WebViewCore::needTouchEvents(bool need)
{
    ALOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");

#if ENABLE(TOUCH_EVENTS)
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;

    if (m_forwardingTouchEvents == need)
        return;

    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_needTouchEvents, need);
    checkException(env);

    m_forwardingTouchEvents = need;
#endif
}

void WebViewCore::requestKeyboard(bool showKeyboard)
{
    ALOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_requestKeyboard, showKeyboard);
    checkException(env);
}

void WebViewCore::notifyProgressFinished()
{
    m_check_domtree_version = true;
    sendNotifyProgressFinished();
}

void WebViewCore::setScrollOffset(int moveGeneration, bool sendScrollEvent, int dx, int dy)
{
    if (m_scrollOffsetX != dx || m_scrollOffsetY != dy) {
        m_scrollOffsetX = dx;
        m_scrollOffsetY = dy;
        // The visible rect is located within our coordinate space so it
        // contains the actual scroll position. Setting the location makes hit
        // testing work correctly.
        m_mainFrame->view()->platformWidget()->setLocation(m_scrollOffsetX,
                m_scrollOffsetY);
        if (sendScrollEvent) {
            m_mainFrame->eventHandler()->sendScrollEvent();

            // Only update history position if it's user scrolled.
            // Update history item to reflect the new scroll position.
            // This also helps save the history information when the browser goes to
            // background, so scroll position will be restored if browser gets
            // killed while in background.
            WebCore::HistoryController* history = m_mainFrame->loader()->history();
            // Because the history item saving could be heavy for large sites and
            // scrolling can generate lots of small scroll offset, the following code
            // reduces the saving frequency.
            static const int MIN_SCROLL_DIFF = 32;
            if (history->currentItem()) {
                WebCore::IntPoint currentPoint = history->currentItem()->scrollPoint();
                if (std::abs(currentPoint.x() - dx) >= MIN_SCROLL_DIFF ||
                    std::abs(currentPoint.y() - dy) >= MIN_SCROLL_DIFF) {
                    history->saveScrollPositionAndViewStateToItem(history->currentItem());
                }
            }
        }

        // update the currently visible screen
        sendPluginVisibleScreen();
    }
}

void WebViewCore::setGlobalBounds(int x, int y, int h, int v)
{
    m_mainFrame->view()->platformWidget()->setWindowBounds(x, y, h, v);
}

void WebViewCore::setSizeScreenWidthAndScale(int width, int height,
    int textWrapWidth, float scale, int screenWidth, int screenHeight,
    int anchorX, int anchorY, bool ignoreHeight)
{
    // Ignore the initial empty document.
    const WebCore::KURL& url = m_mainFrame->document()->url();
    if (url.isEmpty())
        return;

    WebCoreViewBridge* window = m_mainFrame->view()->platformWidget();
    int ow = window->width();
    int oh = window->height();
    int osw = m_screenWidth;
    int osh = m_screenHeight;
    int otw = m_textWrapWidth;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_textWrapWidth = textWrapWidth;
    if (scale >= 0) // negative means keep the current scale
        m_scale = scale;
    m_maxXScroll = screenWidth >> 2;
    m_maxYScroll = m_maxXScroll * height / width;
    // Don't reflow if the diff is small.
    const bool reflow = otw && textWrapWidth &&
        ((float) abs(otw - textWrapWidth) / textWrapWidth) >= 0.01f;

    // When the screen size change, fixed positioned element should be updated.
    // This is supposed to be light weighted operation without a full layout.
    if (osh != screenHeight || osw != screenWidth)
        m_mainFrame->view()->updatePositionedObjects();

    if (ow != width || (!ignoreHeight && oh != height) || reflow) {
        WebCore::RenderObject *r = m_mainFrame->contentRenderer();
        if (r) {
            WebCore::IntPoint anchorPoint = WebCore::IntPoint(anchorX, anchorY);
            RefPtr<WebCore::Node> node;
            WebCore::IntRect bounds;
            WebCore::IntPoint offset;
            // If the text wrap changed, it is probably zoom change or
            // orientation change. Try to keep the anchor at the same place.
            if (otw && textWrapWidth && otw != textWrapWidth &&
                (anchorX != 0 || anchorY != 0)) {
                WebCore::HitTestResult hitTestResult =
                        m_mainFrame->eventHandler()->hitTestResultAtPoint(
                                anchorPoint, false);
                node = hitTestResult.innerNode();
            }
            if (node) {
                bounds = node->getRect();
                // sites like nytimes.com insert a non-standard tag <nyt_text>
                // in the html. If it is the HitTestResult, it may have zero
                // width and height. In this case, use its parent node.
                if (bounds.width() == 0) {
                    node = node->parentOrHostNode();
                    if (node) {
                        bounds = node->getRect();
                    }
                }
            }

            // Set the size after finding the old anchor point as
            // hitTestResultAtPoint causes a layout.
            window->setSize(width, height);
            window->setVisibleSize(screenWidth, screenHeight);
            if (width != screenWidth) {
                m_mainFrame->view()->setUseFixedLayout(true);
                m_mainFrame->view()->setFixedLayoutSize(IntSize(width, height));
            } else
                m_mainFrame->view()->setUseFixedLayout(false);
            r->setNeedsLayoutAndPrefWidthsRecalc();
            if (m_mainFrame->view()->didFirstLayout())
                m_mainFrame->view()->forceLayout();

            // scroll to restore current screen center
            if (node) {
                const WebCore::IntRect& newBounds = node->getRect();
                if ((osw && osh && bounds.width() && bounds.height())
                    && (bounds != newBounds)) {
                    WebCore::FrameView* view = m_mainFrame->view();
                    // force left align if width is not changed while height changed.
                    // the anchorPoint is probably at some white space in the node
                    // which is affected by text wrap around the screen width.
                    const bool leftAlign = (otw != textWrapWidth)
                        && (bounds.width() == newBounds.width())
                        && (bounds.height() != newBounds.height());
                    const float xPercentInDoc =
                        leftAlign ? 0.0 : (float) (anchorX - bounds.x()) / bounds.width();
                    const float xPercentInView =
                        leftAlign ? 0.0 : (float) (anchorX - m_scrollOffsetX) / osw;
                    const float yPercentInDoc = (float) (anchorY - bounds.y()) / bounds.height();
                    const float yPercentInView = (float) (anchorY - m_scrollOffsetY) / osh;
                    showRect(newBounds.x(), newBounds.y(), newBounds.width(),
                             newBounds.height(), view->contentsWidth(),
                             view->contentsHeight(),
                             xPercentInDoc, xPercentInView,
                             yPercentInDoc, yPercentInView);
                }
            }
        }
    } else {
        window->setSize(width, height);
        window->setVisibleSize(screenWidth, screenHeight);
        m_mainFrame->view()->resize(width, height);
        if (width != screenWidth) {
            m_mainFrame->view()->setUseFixedLayout(true);
            m_mainFrame->view()->setFixedLayoutSize(IntSize(width, height));
        } else
            m_mainFrame->view()->setUseFixedLayout(false);
    }

    // update the currently visible screen as perceived by the plugin
    sendPluginVisibleScreen();
}

void WebViewCore::dumpDomTree(bool useFile)
{
#ifdef ANDROID_DOM_LOGGING
    if (useFile)
        gDomTreeFile = fopen(DOM_TREE_LOG_FILE, "w");
    m_mainFrame->document()->showTreeForThis();
    if (gDomTreeFile) {
        fclose(gDomTreeFile);
        gDomTreeFile = 0;
    }
#endif
}

void WebViewCore::dumpRenderTree(bool useFile)
{
#ifdef ANDROID_DOM_LOGGING
    WTF::CString renderDump = WebCore::externalRepresentation(m_mainFrame).utf8();
    const char* data = renderDump.data();
    if (useFile) {
        gRenderTreeFile = fopen(RENDER_TREE_LOG_FILE, "w");
        DUMP_RENDER_LOGD("%s", data);
        fclose(gRenderTreeFile);
        gRenderTreeFile = 0;
    } else {
        // adb log can only output 1024 characters, so write out line by line.
        // exclude '\n' as adb log adds it for each output.
        int length = renderDump.length();
        for (int i = 0, last = 0; i < length; i++) {
            if (data[i] == '\n') {
                if (i != last)
                    DUMP_RENDER_LOGD("%.*s", (i - last), &(data[last]));
                last = i + 1;
            }
        }
    }
#endif
}

HTMLElement* WebViewCore::retrieveElement(int x, int y,
    const QualifiedName& tagName)
{
    HitTestResult hitTestResult = m_mainFrame->eventHandler()
        ->hitTestResultAtPoint(IntPoint(x, y), false, false,
        DontHitTestScrollbars, HitTestRequest::Active | HitTestRequest::ReadOnly,
        IntSize(1, 1));
    if (!hitTestResult.innerNode() || !hitTestResult.innerNode()->inDocument()) {
        ALOGE("Should not happen: no in document Node found");
        return 0;
    }
    const ListHashSet<RefPtr<Node> >& list = hitTestResult.rectBasedTestResult();
    if (list.isEmpty()) {
        ALOGE("Should not happen: no rect-based-test nodes found");
        return 0;
    }
    Node* node = hitTestResult.innerNode();
    Node* element = node;
    while (element && (!element->isElementNode()
        || !element->hasTagName(tagName))) {
        element = element->parentNode();
    }
    return static_cast<WebCore::HTMLElement*>(element);
}

HTMLAnchorElement* WebViewCore::retrieveAnchorElement(int x, int y)
{
    return static_cast<HTMLAnchorElement*>
        (retrieveElement(x, y, HTMLNames::aTag));
}

HTMLImageElement* WebViewCore::retrieveImageElement(int x, int y)
{
    return static_cast<HTMLImageElement*>
        (retrieveElement(x, y, HTMLNames::imgTag));
}

WTF::String WebViewCore::retrieveHref(int x, int y)
{
    // TODO: This is expensive, cache
    HitTestResult result = m_mainFrame->eventHandler()->hitTestResultAtPoint(IntPoint(x, y),
                false, false, DontHitTestScrollbars, HitTestRequest::Active | HitTestRequest::ReadOnly, IntSize(1, 1));
    return result.absoluteLinkURL();
}

WTF::String WebViewCore::retrieveAnchorText(int x, int y)
{
    WebCore::HTMLAnchorElement* anchor = retrieveAnchorElement(x, y);
    return anchor ? anchor->text() : WTF::String();
}

WTF::String WebViewCore::retrieveImageSource(int x, int y)
{
    // TODO: This is expensive, cache
    HitTestResult result = m_mainFrame->eventHandler()->hitTestResultAtPoint(IntPoint(x, y),
                false, false, DontHitTestScrollbars, HitTestRequest::Active | HitTestRequest::ReadOnly, IntSize(1, 1));
    return result.absoluteImageURL();
}

WTF::String WebViewCore::requestLabel(WebCore::Frame* frame,
        WebCore::Node* node)
{
    if (node && validNode(m_mainFrame, frame, node)) {
        RefPtr<WebCore::NodeList> list = node->document()->getElementsByTagName("label");
        unsigned length = list->length();
        for (unsigned i = 0; i < length; i++) {
            WebCore::HTMLLabelElement* label = static_cast<WebCore::HTMLLabelElement*>(
                    list->item(i));
            if (label->control() == node) {
                Node* node = label;
                String result;
                while ((node = node->traverseNextNode(label))) {
                    if (node->isTextNode()) {
                        Text* textNode = static_cast<Text*>(node);
                        result += textNode->dataImpl();
                    }
                }
                return result;
            }
        }
    }
    return WTF::String();
}

static bool isContentEditable(const WebCore::Node* node)
{
    if (!node)
        return false;
    Frame* frame = node->document()->frame();
    if (!frame)
        return false;
    return frame->selection()->isContentEditable();
}

// Returns true if the node is a textfield, textarea, or contentEditable
static bool isTextInput(const WebCore::Node* node)
{
    if (isContentEditable(node))
        return true;
    if (!node)
        return false;
    WebCore::RenderObject* renderer = node->renderer();
    return renderer && (renderer->isTextField() || renderer->isTextArea());
}

void WebViewCore::revealSelection()
{
    WebCore::Node* focus = currentFocus();
    if (!focus)
        return;
    if (!isTextInput(focus))
        return;
    WebCore::Frame* focusedFrame = focus->document()->frame();
    if (!focusedFrame->page()->focusController()->isActive())
        return;
    focusedFrame->selection()->revealSelection(ScrollAlignment::alignToEdgeIfNeeded);
}

struct TouchNodeData {
    Node* mUrlNode;
    Node* mInnerNode;
    IntRect mBounds;
};

// get the bounding box of the Node
static IntRect getAbsoluteBoundingBox(Node* node) {
    IntRect rect;
    RenderObject* render = node->renderer();
    if (!render)
        return rect;
    if (render->isRenderInline())
        rect = toRenderInline(render)->linesVisualOverflowBoundingBox();
    else if (render->isBox())
        rect = toRenderBox(render)->visualOverflowRect();
    else if (render->isText())
        rect = toRenderText(render)->linesBoundingBox();
    else
        ALOGE("getAbsoluteBoundingBox failed for node %p, name %s", node, render->renderName());
    FloatPoint absPos = render->localToAbsolute(FloatPoint(), false, true);
    rect.move(absPos.x(), absPos.y());
    return rect;
}

WebCore::Frame* WebViewCore::focusedFrame() const
{
    return m_mainFrame->page()->focusController()->focusedOrMainFrame();
}

VisiblePosition WebViewCore::visiblePositionForContentPoint(int x, int y)
{
    return visiblePositionForContentPoint(IntPoint(x, y));
}

VisiblePosition WebViewCore::visiblePositionForContentPoint(const IntPoint& point)
{
    // Hit test of this kind required for this to work inside input fields
    HitTestRequest request(HitTestRequest::Active
                           | HitTestRequest::MouseMove
                           | HitTestRequest::ReadOnly
                           | HitTestRequest::IgnoreClipping);
    HitTestResult result(point);
    focusedFrame()->document()->renderView()->layer()->hitTest(request, result);

    // Matching the logic in MouseEventWithHitTestResults::targetNode()
    Node* node = result.innerNode();
    if (!node)
        return VisiblePosition();
    Element* element = node->parentElement();
    if (!node->inDocument() && element && element->inDocument())
        node = element;

    return node->renderer()->positionForPoint(result.localPoint());
}

void WebViewCore::selectWordAt(int x, int y)
{
    HitTestResult hoverResult;
    moveMouse(m_mainFrame, x, y, &hoverResult);
    if (hoverResult.innerNode()) {
        Node* node = hoverResult.innerNode();
        Frame* frame = node->document()->frame();
        Page* page = m_mainFrame->document()->page();
        page->focusController()->setFocusedFrame(frame);
    }

    IntPoint point = convertGlobalContentToFrameContent(IntPoint(x, y));

    // Hit test of this kind required for this to work inside input fields
    HitTestRequest request(HitTestRequest::Active);
    HitTestResult result(point);

    focusedFrame()->document()->renderView()->layer()->hitTest(request, result);

    // Matching the logic in MouseEventWithHitTestResults::targetNode()
    Node* node = result.innerNode();
    if (!node)
        return;
    Element* element = node->parentElement();
    if (!node->inDocument() && element && element->inDocument())
        node = element;

    SelectionController* sc = focusedFrame()->selection();
    if (!sc->contains(point) && (node->isContentEditable() || node->isTextNode()) && !result.isLiveLink()
            && node->dispatchEvent(Event::create(eventNames().selectstartEvent, true, true))) {
        VisiblePosition pos(node->renderer()->positionForPoint(result.localPoint()));
        selectWordAroundPosition(node->document()->frame(), pos);
    }
}

void WebViewCore::selectWordAroundPosition(Frame* frame, VisiblePosition pos)
{
    VisibleSelection selection(pos);
    selection.expandUsingGranularity(WordGranularity);

    if (frame->selection()->shouldChangeSelection(selection)) {
        bool allWhitespaces = true;
        RefPtr<Range> firstRange = selection.firstRange();
        String text = firstRange.get() ? firstRange->text() : "";
        for (size_t i = 0; i < text.length(); ++i) {
            if (!isSpaceOrNewline(text[i])) {
                allWhitespaces = false;
                break;
            }
        }

        if (allWhitespaces) {
            VisibleSelection emptySelection(selection.visibleStart(), selection.visibleStart());
            frame->selection()->setSelection(emptySelection);
        }
        frame->selection()->setSelection(selection);
    }
}

int WebViewCore::platformLayerIdFromNode(Node* node, LayerAndroid** outLayer)
{
    if (!node || !node->renderer())
        return -1;
    RenderLayer* renderLayer = node->renderer()->enclosingLayer();
    if (!renderLayer || !renderLayer->isComposited())
        return -1;
    GraphicsLayer* graphicsLayer = renderLayer->backing()->graphicsLayer();
    if (!graphicsLayer)
        return -1;
    GraphicsLayerAndroid* agl = static_cast<GraphicsLayerAndroid*>(graphicsLayer);
    LayerAndroid* layer = agl->foregroundLayer();
    if (!layer)
        layer = agl->contentLayer();
    if (!layer)
        return -1;
    if (outLayer)
        *outLayer = layer;
    return layer->uniqueId();
}

void WebViewCore::layerToAbsoluteOffset(const LayerAndroid* layer, IntPoint& offset)
{
    while (layer) {
        const SkPoint& pos = layer->getPosition();
        offset.move(pos.fX, pos.fY);
        const IntPoint& scroll = layer->scrollOffset();
        offset.move(-scroll.x(), -scroll.y());
        layer = static_cast<LayerAndroid*>(layer->getParent());
    }
}

SelectText* WebViewCore::createSelectText(const VisibleSelection& selection)
{
    // We need to agressively check to see if this is an empty selection to prevent
    // accidentally entering text selection mode
    bool isCaret = selection.isCaret();
    if (selection.isNone() || (!selection.isContentEditable() && isCaret))
        return 0;

    RefPtr<Range> range = selection.firstRange();
    Node* startContainer = range->startContainer();
    Node* endContainer = range->endContainer();

    if (!startContainer || !endContainer)
        return 0;
    if (!isCaret && startContainer == endContainer
            && range->startOffset() == range->endOffset())
        return 0;

    SelectText* selectTextContainer = new SelectText();
    IntPoint frameOffset = convertGlobalContentToFrameContent(IntPoint());

    IntRect startHandle;
    IntRect endHandle;
    if (isCaret) {
        // Caret selection
        Position start = selection.start();
        Node* node = start.anchorNode();
        LayerAndroid* layer = 0;
        int layerId = platformLayerIdFromNode(node, &layer);
        selectTextContainer->setCaretLayerId(SelectText::EndHandle, layerId);
        selectTextContainer->setCaretLayerId(SelectText::StartHandle, layerId);
        IntPoint layerOffset;
        layerToAbsoluteOffset(layer, layerOffset);
        RenderObject* r = node->renderer();
        RenderText* renderText = toRenderText(r);
        int caretOffset;
        InlineBox* inlineBox;
        start.getInlineBoxAndOffset(DOWNSTREAM, inlineBox, caretOffset);
        startHandle = renderText->localCaretRect(inlineBox, caretOffset);
        FloatPoint absoluteOffset = renderText->localToAbsolute(startHandle.location());
        startHandle.setX(absoluteOffset.x() - layerOffset.x());
        startHandle.setY(absoluteOffset.y() - layerOffset.y());
        endHandle = startHandle;
    } else {
        // Selected range
        Node* stopNode = range->pastLastNode();
        for (Node* node = range->firstNode(); node != stopNode; node = node->traverseNextNode()) {
            RenderObject* r = node->renderer();
            if (!r || !r->isText() || r->style()->visibility() != VISIBLE)
                continue;
            RenderText* renderText = toRenderText(r);
            int startOffset = node == startContainer ? range->startOffset() : 0;
            int endOffset = node == endContainer ? range->endOffset() : numeric_limits<int>::max();
            LayerAndroid* layer = 0;
            int layerId = platformLayerIdFromNode(node, &layer);
            Vector<IntRect> rects;
            renderText->absoluteRectsForRange(rects, startOffset, endOffset, true);
            if (rects.size()) {
                IntPoint offset;
                layerToAbsoluteOffset(layer, offset);
                endHandle = rects[rects.size() - 1];
                endHandle.move(-offset.x(), -offset.y());
                selectTextContainer->setCaretLayerId(SelectText::EndHandle, layerId);
                if (startHandle.isEmpty()) {
                    startHandle = rects[0];
                    startHandle.move(-offset.x(), -offset.y());
                    selectTextContainer->setCaretLayerId(SelectText::StartHandle, layerId);
                }
            }
            selectTextContainer->addHighlightRegion(layer, rects, frameOffset);
        }
    }

    selectTextContainer->setBaseFirst(selection.isBaseFirst());

    // Squish the handle rects
    startHandle.setWidth(1);
    endHandle.move(endHandle.width() - 1, 0);
    endHandle.setWidth(1);
    startHandle.move(-frameOffset.x(), -frameOffset.y());
    selectTextContainer->setCaretRect(SelectText::StartHandle, startHandle);
    endHandle.move(-frameOffset.x(), -frameOffset.y());
    selectTextContainer->setCaretRect(SelectText::EndHandle, endHandle);

    selectTextContainer->setText(range->text());

    return selectTextContainer;
}

IntPoint WebViewCore::convertGlobalContentToFrameContent(const IntPoint& point, WebCore::Frame* frame)
{
    if (!frame) frame = focusedFrame();
    IntPoint frameOffset(-m_scrollOffsetX, -m_scrollOffsetY);
    frameOffset = frame->view()->windowToContents(frameOffset);
    return IntPoint(point.x() + frameOffset.x(), point.y() + frameOffset.y());
}

void WebViewCore::selectText(int startX, int startY, int endX, int endY)
{
    SelectionController* sc = focusedFrame()->selection();
    IntPoint startPoint = convertGlobalContentToFrameContent(IntPoint(startX, startY));
    VisiblePosition startPosition(visiblePositionForContentPoint(startPoint));
    IntPoint endPoint = convertGlobalContentToFrameContent(IntPoint(endX, endY));
    VisiblePosition endPosition(visiblePositionForContentPoint(endPoint));

    if (startPosition.isNull() || endPosition.isNull())
        return;

    // Ensure startPosition is before endPosition
    if (comparePositions(startPosition, endPosition) > 0)
        swap(startPosition, endPosition);

    if (sc->isContentEditable()) {
        startPosition = sc->selection().visibleStart().honorEditableBoundaryAtOrAfter(startPosition);
        endPosition = sc->selection().visibleEnd().honorEditableBoundaryAtOrBefore(endPosition);
        if (startPosition.isNull() || endPosition.isNull()) {
            return;
        }
    }

    // Ensure startPosition is not at end of block
    if (startPosition != endPosition && isEndOfBlock(startPosition)) {
        VisiblePosition nextStartPosition(startPosition.next());
        if (!nextStartPosition.isNull())
            startPosition = nextStartPosition;
    }
    // Ensure endPosition is not at start of block
    if (startPosition != endPosition && isStartOfBlock(endPosition)) {
        VisiblePosition prevEndPosition(endPosition.previous());
        if (!prevEndPosition.isNull())
            endPosition = prevEndPosition;
    }

    VisibleSelection selection(startPosition, endPosition);
    // Only allow changes between caret positions or to text selection.
    bool selectChangeAllowed = (!selection.isCaret() || sc->isCaret());
    if (selectChangeAllowed && sc->shouldChangeSelection(selection))
        sc->setSelection(selection);
}

// get the highlight rectangles for the touch point (x, y) with the slop
AndroidHitTestResult WebViewCore::hitTestAtPoint(int x, int y, int slop, bool doMoveMouse)
{
    if (doMoveMouse)
        moveMouse(m_mainFrame, x, y);
    HitTestResult hitTestResult = m_mainFrame->eventHandler()->hitTestResultAtPoint(IntPoint(x, y),
            false, false, DontHitTestScrollbars, HitTestRequest::Active | HitTestRequest::ReadOnly, IntSize(slop, slop));
    AndroidHitTestResult androidHitResult(this, hitTestResult);
    if (!hitTestResult.innerNode() || !hitTestResult.innerNode()->inDocument()) {
        ALOGE("Should not happen: no in document Node found");
        return androidHitResult;
    }
    const ListHashSet<RefPtr<Node> >& list = hitTestResult.rectBasedTestResult();
    if (list.isEmpty()) {
        ALOGE("Should not happen: no rect-based-test nodes found");
        return androidHitResult;
    }
    Frame* frame = hitTestResult.innerNode()->document()->frame();
    Vector<TouchNodeData> nodeDataList;
    if (hitTestResult.innerNode() != hitTestResult.innerNonSharedNode()
            && hitTestResult.innerNode()->hasTagName(WebCore::HTMLNames::areaTag)) {
        HTMLAreaElement* area = static_cast<HTMLAreaElement*>(hitTestResult.innerNode());
        androidHitResult.hitTestResult().setURLElement(area);
        androidHitResult.highlightRects().append(area->computeRect(
                hitTestResult.innerNonSharedNode()->renderer()));
        return androidHitResult;
    }
    ListHashSet<RefPtr<Node> >::const_iterator last = list.end();
    for (ListHashSet<RefPtr<Node> >::const_iterator it = list.begin(); it != last; ++it) {
        // TODO: it seems reasonable to not search across the frame. Isn't it?
        // if the node is not in the same frame as the innerNode, skip it
        if (it->get()->document()->frame() != frame)
            continue;
        // traverse up the tree to find the first node that needs highlight
        bool found = false;
        Node* eventNode = it->get();
        Node* innerNode = eventNode;
        while (eventNode) {
            RenderObject* render = eventNode->renderer();
            if (render && (render->isBody() || render->isRenderView()))
                break;
            if (eventNode->supportsFocus()
                    || eventNode->hasEventListeners(eventNames().clickEvent)
                    || eventNode->hasEventListeners(eventNames().mousedownEvent)
                    || eventNode->hasEventListeners(eventNames().mouseupEvent)
                    || eventNode->hasEventListeners(eventNames().mouseoverEvent)) {
                found = true;
                break;
            }
            // the nodes in the rectBasedTestResult() are ordered based on z-index during hit testing.
            // so do not search for the eventNode across explicit z-index border.
            // TODO: this is a hard one to call. z-index is quite complicated as its value only
            // matters when you compare two RenderLayer in the same hierarchy level. e.g. in
            // the following example, "b" is on the top as its z level is the highest. even "c"
            // has 100 as z-index, it is still below "d" as its parent has the same z-index as
            // "d" and logically before "d". Of course "a" is the lowest in the z level.
            //
            // z-index:auto "a"
            //   z-index:2 "b"
            //   z-index:1
            //     z-index:100 "c"
            //   z-index:1 "d"
            //
            // If the fat point touches everyone, the order in the list should be "b", "d", "c"
            // and "a". When we search for the event node for "b", we really don't want "a" as
            // in the z-order it is behind everything else.
            if (render && !render->style()->hasAutoZIndex())
                break;
            eventNode = eventNode->parentNode();
        }
        // didn't find any eventNode, skip it
        if (!found)
            continue;
        // first quick check whether it is a duplicated node before computing bounding box
        Vector<TouchNodeData>::const_iterator nlast = nodeDataList.end();
        for (Vector<TouchNodeData>::const_iterator n = nodeDataList.begin(); n != nlast; ++n) {
            // found the same node, skip it
            if (eventNode == n->mUrlNode) {
                found = false;
                break;
            }
        }
        if (!found)
            continue;
        // next check whether the node is fully covered by or fully covering another node.
        found = false;
        IntRect rect = getAbsoluteBoundingBox(eventNode);
        if (rect.isEmpty()) {
            // if the node's bounds is empty and it is not a ContainerNode, skip it.
            if (!eventNode->isContainerNode())
                continue;
            // if the node's children are all positioned objects, its bounds can be empty.
            // Walk through the children to find the bounding box.
            Node* child = static_cast<const ContainerNode*>(eventNode)->firstChild();
            while (child) {
                IntRect childrect;
                if (child->renderer())
                    childrect = getAbsoluteBoundingBox(child);
                if (!childrect.isEmpty()) {
                    rect.unite(childrect);
                    child = child->traverseNextSibling(eventNode);
                } else
                    child = child->traverseNextNode(eventNode);
            }
        }
        for (int i = nodeDataList.size() - 1; i >= 0; i--) {
            TouchNodeData n = nodeDataList.at(i);
            // the new node is enclosing an existing node, skip it
            if (rect.contains(n.mBounds)) {
                found = true;
                break;
            }
            // the new node is fully inside an existing node, remove the existing node
            if (n.mBounds.contains(rect))
                nodeDataList.remove(i);
        }
        if (!found) {
            TouchNodeData newNode;
            newNode.mUrlNode = eventNode;
            newNode.mBounds = rect;
            newNode.mInnerNode = innerNode;
            nodeDataList.append(newNode);
        }
    }
    if (!nodeDataList.size()) {
        androidHitResult.searchContentDetectors();
        return androidHitResult;
    }
    // finally select the node with the largest overlap with the fat point
    TouchNodeData final;
    final.mUrlNode = 0;
    IntPoint docPos = frame->view()->windowToContents(m_mousePos);
    IntRect testRect(docPos.x() - slop, docPos.y() - slop, 2 * slop + 1, 2 * slop + 1);
    int area = 0;
    Vector<TouchNodeData>::const_iterator nlast = nodeDataList.end();
    for (Vector<TouchNodeData>::const_iterator n = nodeDataList.begin(); n != nlast; ++n) {
        IntRect rect = n->mBounds;
        rect.intersect(testRect);
        int a = rect.width() * rect.height();
        if (a > area || !final.mUrlNode) {
            final = *n;
            area = a;
        }
    }
    // now get the node's highlight rectangles in the page coordinate system
    if (final.mUrlNode) {
        if (final.mUrlNode->isElementNode()) {
            // We found a URL element. Update the hitTestResult
            androidHitResult.setURLElement(static_cast<Element*>(final.mUrlNode));
        } else {
            androidHitResult.setURLElement(0);
        }
        // Update innerNode and innerNonSharedNode
        androidHitResult.hitTestResult().setInnerNode(final.mInnerNode);
        androidHitResult.hitTestResult().setInnerNonSharedNode(final.mInnerNode);
        IntPoint frameAdjust;
        if (frame != m_mainFrame) {
            frameAdjust = frame->view()->contentsToWindow(IntPoint());
            frameAdjust.move(m_scrollOffsetX, m_scrollOffsetY);
        }
        IntRect rect = final.mBounds;
        rect.move(frameAdjust.x(), frameAdjust.y());
        if (doMoveMouse) {
            // adjust m_mousePos if it is not inside the returned highlight rectangle
            testRect.move(frameAdjust.x(), frameAdjust.y());
            testRect.intersect(rect);
            if (!testRect.contains(x, y))
                moveMouse(m_mainFrame, testRect.center().x(), testRect.center().y());
        }
    } else {
        androidHitResult.searchContentDetectors();
    }
    return androidHitResult;
}

///////////////////////////////////////////////////////////////////////////////

void WebViewCore::addPlugin(PluginWidgetAndroid* w)
{
//    SkDebugf("----------- addPlugin %p", w);
    /* The plugin must be appended to the end of the array. This ensures that if
       the plugin is added while iterating through the array (e.g. sendEvent(...))
       that the iteration process is not corrupted.
     */
    *m_plugins.append() = w;
}

void WebViewCore::removePlugin(PluginWidgetAndroid* w)
{
//    SkDebugf("----------- removePlugin %p", w);
    int index = m_plugins.find(w);
    if (index < 0) {
        SkDebugf("--------------- pluginwindow not found! %p\n", w);
    } else {
        m_plugins.removeShuffle(index);
    }
}

bool WebViewCore::isPlugin(PluginWidgetAndroid* w) const
{
    return m_plugins.find(w) >= 0;
}

void WebViewCore::invalPlugin(PluginWidgetAndroid* w)
{
    const double PLUGIN_INVAL_DELAY = 1.0 / 60;

    if (!m_pluginInvalTimer.isActive()) {
        m_pluginInvalTimer.startOneShot(PLUGIN_INVAL_DELAY);
    }
}

void WebViewCore::drawPlugins()
{
    SkRegion inval; // accumualte what needs to be redrawn
    PluginWidgetAndroid** iter = m_plugins.begin();
    PluginWidgetAndroid** stop = m_plugins.end();

    for (; iter < stop; ++iter) {
        PluginWidgetAndroid* w = *iter;
        SkIRect dirty;
        if (w->isDirty(&dirty)) {
            w->draw();
            inval.op(dirty, SkRegion::kUnion_Op);
        }
    }

    if (!inval.isEmpty()) {
        // inval.getBounds() is our rectangle
        const SkIRect& bounds = inval.getBounds();
        WebCore::IntRect r(bounds.fLeft, bounds.fTop,
                           bounds.width(), bounds.height());
        this->viewInvalidate(r);
    }
}

void WebViewCore::notifyPluginsOnFrameLoad(const Frame* frame) {
    // if frame is the parent then notify all plugins
    if (!frame->tree()->parent()) {
        // trigger an event notifying the plugins that the page has loaded
        ANPEvent event;
        SkANP::InitEvent(&event, kLifecycle_ANPEventType);
        event.data.lifecycle.action = kOnLoad_ANPLifecycleAction;
        sendPluginEvent(event);
        // trigger the on/off screen notification if the page was reloaded
        sendPluginVisibleScreen();
    }
    // else if frame's parent has completed
    else if (!frame->tree()->parent()->loader()->isLoading()) {
        // send to all plugins who have this frame in their heirarchy
        PluginWidgetAndroid** iter = m_plugins.begin();
        PluginWidgetAndroid** stop = m_plugins.end();
        for (; iter < stop; ++iter) {
            Frame* currentFrame = (*iter)->pluginView()->parentFrame();
            while (currentFrame) {
                if (frame == currentFrame) {
                    ANPEvent event;
                    SkANP::InitEvent(&event, kLifecycle_ANPEventType);
                    event.data.lifecycle.action = kOnLoad_ANPLifecycleAction;
                    (*iter)->sendEvent(event);

                    // trigger the on/off screen notification if the page was reloaded
                    ANPRectI visibleRect;
                    getVisibleScreen(visibleRect);
                    (*iter)->setVisibleScreen(visibleRect, m_scale);

                    break;
                }
                currentFrame = currentFrame->tree()->parent();
            }
        }
    }
}

void WebViewCore::getVisibleScreen(ANPRectI& visibleRect)
{
    visibleRect.left = m_scrollOffsetX;
    visibleRect.top = m_scrollOffsetY;
    visibleRect.right = m_scrollOffsetX + m_screenWidth;
    visibleRect.bottom = m_scrollOffsetY + m_screenHeight;
}

void WebViewCore::sendPluginVisibleScreen()
{
    /* We may want to cache the previous values and only send the notification
       to the plugin in the event that one of the values has changed.
     */

    ANPRectI visibleRect;
    getVisibleScreen(visibleRect);

    PluginWidgetAndroid** iter = m_plugins.begin();
    PluginWidgetAndroid** stop = m_plugins.end();
    for (; iter < stop; ++iter) {
        (*iter)->setVisibleScreen(visibleRect, m_scale);
    }
}

void WebViewCore::sendPluginSurfaceReady()
{
    PluginWidgetAndroid** iter = m_plugins.begin();
    PluginWidgetAndroid** stop = m_plugins.end();
    for (; iter < stop; ++iter) {
        (*iter)->checkSurfaceReady();
    }
}

void WebViewCore::sendPluginEvent(const ANPEvent& evt)
{
    /* The list of plugins may be manipulated as we iterate through the list.
       This implementation allows for the addition of new plugins during an
       iteration, but may fail if a plugin is removed. Currently, there are not
       any use cases where a plugin is deleted while processing this loop, but
       if it does occur we will have to use an alternate data structure and/or
       iteration mechanism.
     */
    for (int x = 0; x < m_plugins.count(); x++) {
        m_plugins[x]->sendEvent(evt);
    }
}

PluginWidgetAndroid* WebViewCore::getPluginWidget(NPP npp)
{
    PluginWidgetAndroid** iter = m_plugins.begin();
    PluginWidgetAndroid** stop = m_plugins.end();
    for (; iter < stop; ++iter) {
        if ((*iter)->pluginView()->instance() == npp) {
            return (*iter);
        }
    }
    return 0;
}

static PluginView* nodeIsPlugin(Node* node) {
    RenderObject* renderer = node->renderer();
    if (renderer && renderer->isWidget()) {
        Widget* widget = static_cast<RenderWidget*>(renderer)->widget();
        if (widget && widget->isPluginView())
            return static_cast<PluginView*>(widget);
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
void WebViewCore::moveMouseIfLatest(int moveGeneration,
    WebCore::Frame* frame, int x, int y)
{
    if (m_moveGeneration > moveGeneration) {
        return; // short-circuit if a newer move has already been generated
    }
    m_lastGeneration = moveGeneration;
    moveMouse(frame, x, y);
}

void WebViewCore::moveFocus(WebCore::Frame* frame, WebCore::Node* node)
{
    if (!node || !validNode(m_mainFrame, frame, node)
            || !node->isElementNode())
        return;
    // Code borrowed from FocusController::advanceFocus
    WebCore::FocusController* focusController
            = m_mainFrame->page()->focusController();
    WebCore::Document* oldDoc
            = focusController->focusedOrMainFrame()->document();
    if (oldDoc->focusedNode() == node)
        return;
    if (node->document() != oldDoc)
        oldDoc->setFocusedNode(0);
    focusController->setFocusedFrame(frame);
    static_cast<WebCore::Element*>(node)->focus(false);
}

// Update mouse position
void WebViewCore::moveMouse(WebCore::Frame* frame, int x, int y, HitTestResult* hoveredNode)
{
    if (!frame || !validNode(m_mainFrame, frame, 0))
        frame = m_mainFrame;
    // mouse event expects the position in the window coordinate
    m_mousePos = WebCore::IntPoint(x - m_scrollOffsetX, y - m_scrollOffsetY);
    // validNode will still return true if the node is null, as long as we have
    // a valid frame.  Do not want to make a call on frame unless it is valid.
    WebCore::PlatformMouseEvent mouseEvent(m_mousePos, m_mousePos,
        WebCore::NoButton, WebCore::MouseEventMoved, 1, false, false, false,
        false, WTF::currentTime());
    frame->eventHandler()->handleMouseMoveEvent(mouseEvent, hoveredNode);
}

Position WebViewCore::getPositionForOffset(Node* node, int offset)
{
    Position start = firstPositionInNode(node);
    Position end = lastPositionInNode(node);
    Document* document = node->document();
    PassRefPtr<Range> range = Range::create(document, start, end);
    WebCore::CharacterIterator iterator(range.get());
    iterator.advance(offset);
    return iterator.range()->startPosition();
}

void WebViewCore::setSelection(Node* node, int start, int end)
{
    RenderTextControl* control = toRenderTextControl(node);
    if (control)
        setSelectionRange(node, start, end);
    else {
        Position startPosition = getPositionForOffset(node, start);
        Position endPosition = getPositionForOffset(node, end);
        VisibleSelection selection(startPosition, endPosition);
        SelectionController* selector = node->document()->frame()->selection();
        selector->setSelection(selection);
    }
}

void WebViewCore::setSelection(int start, int end)
{
    WebCore::Node* focus = currentFocus();
    if (!focus)
        return;
    if (start > end)
        swap(start, end);

    // Tell our EditorClient that this change was generated from the UI, so it
    // does not need to echo it to the UI.
    EditorClientAndroid* client = static_cast<EditorClientAndroid*>(
            m_mainFrame->editor()->client());
    client->setUiGeneratedSelectionChange(true);
    setSelection(focus, start, end);
    RenderTextControl* control = toRenderTextControl(focus);
    if (start != end && control) {
        // Fire a select event. No event is sent when the selection reduces to
        // an insertion point
        control->selectionChanged(true);
    }
    client->setUiGeneratedSelectionChange(false);
    bool isPasswordField = false;
    if (focus->isElementNode()) {
        WebCore::Element* element = static_cast<WebCore::Element*>(focus);
        if (WebCore::InputElement* inputElement = element->toInputElement())
            isPasswordField = static_cast<WebCore::HTMLInputElement*>(inputElement)->isPasswordField();
    }
    // For password fields, this is done in the UI side via
    // bringPointIntoView, since the UI does the drawing.
    if ((control && control->isTextArea()) || !isPasswordField)
        revealSelection();
}

String WebViewCore::modifySelection(const int direction, const int axis)
{
    DOMSelection* selection = m_mainFrame->domWindow()->getSelection();
    ASSERT(selection);
    // We've seen crashes where selection is null, but we don't know why
    // See http://b/5244036
    if (!selection)
        return String();
    if (selection->rangeCount() > 1)
        selection->removeAllRanges();
    switch (axis) {
        case AXIS_CHARACTER:
        case AXIS_WORD:
        case AXIS_SENTENCE:
            return modifySelectionTextNavigationAxis(selection, direction, axis);
        case AXIS_HEADING:
        case AXIS_SIBLING:
        case AXIS_PARENT_FIRST_CHILD:
        case AXIS_DOCUMENT:
            return modifySelectionDomNavigationAxis(selection, direction, axis);
        default:
            ALOGE("Invalid navigation axis: %d", axis);
            return String();
    }
}

void WebViewCore::scrollNodeIntoView(Frame* frame, Node* node)
{
    if (!frame || !node)
        return;

    Element* elementNode = 0;

    // If not an Element, find a visible predecessor
    // Element to scroll into view.
    if (!node->isElementNode()) {
        HTMLElement* body = frame->document()->body();
        do {
            if (node == body)
                return;
            node = node->parentNode();
        } while (node && !node->isElementNode() && !isVisible(node));
    }

    // Couldn't find a visible predecessor.
    if (!node)
        return;

    elementNode = static_cast<Element*>(node);
    elementNode->scrollIntoViewIfNeeded(true);
}

String WebViewCore::modifySelectionTextNavigationAxis(DOMSelection* selection, int direction, int axis)
{
    Node* body = m_mainFrame->document()->body();

    ExceptionCode ec = 0;
    String markup;

    // initialize the selection if necessary
    if (selection->rangeCount() == 0) {
        if (m_currentNodeDomNavigationAxis
                && validNode(m_mainFrame,
                m_mainFrame, m_currentNodeDomNavigationAxis)) {
            RefPtr<Range> rangeRef =
                selection->frame()->document()->createRange();
            rangeRef->selectNode(m_currentNodeDomNavigationAxis, ec);
            m_currentNodeDomNavigationAxis = 0;
            if (ec)
                return String();
            selection->addRange(rangeRef.get());
        } else if (currentFocus()) {
            selection->setPosition(currentFocus(), 0, ec);
        } else {
            selection->setPosition(body, 0, ec);
        }
        if (ec)
            return String();
    }

    // collapse the selection
    if (direction == DIRECTION_FORWARD)
        selection->collapseToEnd(ec);
    else
        selection->collapseToStart(ec);
    if (ec)
        return String();

    // Make sure the anchor node is a text node since we are generating
    // the markup of the selection which includes the anchor, the focus,
    // and any crossed nodes. Forcing the condition that the selection
    // starts and ends on text nodes guarantees symmetric selection markup.
    // Also this way the text content, rather its container, is highlighted.
    Node* anchorNode = selection->anchorNode();
    if (anchorNode->isElementNode()) {
        // Collapsed selection while moving forward points to the
        // next unvisited node and while moving backward to the
        // last visited node.
        if (direction == DIRECTION_FORWARD)
            advanceAnchorNode(selection, direction, markup, false, ec);
        else
            advanceAnchorNode(selection, direction, markup, true, ec);
        if (ec)
            return String();
        if (!markup.isEmpty())
            return markup;
    }

    // If the selection is at the end of a non white space text move
    // it to the next visible text node with non white space content.
    // This is a workaround for the selection getting stuck.
    anchorNode = selection->anchorNode();
    if (anchorNode->isTextNode()) {
        if (direction == DIRECTION_FORWARD) {
            String suffix = anchorNode->textContent().substring(
                    selection->anchorOffset(), caretMaxOffset(anchorNode));
            // If at the end of non white space text we advance the
            // anchor node to either an input element or non empty text.
            if (suffix.stripWhiteSpace().isEmpty()) {
                advanceAnchorNode(selection, direction, markup, true, ec);
            }
        } else {
            String prefix = anchorNode->textContent().substring(0,
                    selection->anchorOffset());
            // If at the end of non white space text we advance the
            // anchor node to either an input element or non empty text.
            if (prefix.stripWhiteSpace().isEmpty()) {
                advanceAnchorNode(selection, direction, markup, true, ec);
            }
        }
        if (ec)
            return String();
        if (!markup.isEmpty())
            return markup;
    }

    // extend the selection
    String directionStr;
    if (direction == DIRECTION_FORWARD)
        directionStr = "forward";
    else
        directionStr = "backward";

    String axisStr;
    if (axis == AXIS_CHARACTER)
        axisStr = "character";
    else if (axis == AXIS_WORD)
        axisStr = "word";
    else
        axisStr = "sentence";

    selection->modify("extend", directionStr, axisStr);

    // Make sure the focus node is a text node in order to have the
    // selection generate symmetric markup because the latter
    // includes all nodes crossed by the selection.  Also this way
    // the text content, rather its container, is highlighted.
    Node* focusNode = selection->focusNode();
    if (focusNode->isElementNode()) {
        focusNode = getImplicitBoundaryNode(selection->focusNode(),
                selection->focusOffset(), direction);
        if (!focusNode)
            return String();
        if (direction == DIRECTION_FORWARD) {
            focusNode = focusNode->traversePreviousSiblingPostOrder(body);
            if (focusNode && !isContentTextNode(focusNode)) {
                Node* textNode = traverseNextContentTextNode(focusNode,
                        anchorNode, DIRECTION_BACKWARD);
                if (textNode)
                    anchorNode = textNode;
            }
            if (focusNode && isContentTextNode(focusNode)) {
                selection->extend(focusNode, caretMaxOffset(focusNode), ec);
                if (ec)
                    return String();
            }
        } else {
            focusNode = focusNode->traverseNextSibling();
            if (focusNode && !isContentTextNode(focusNode)) {
                Node* textNode = traverseNextContentTextNode(focusNode,
                        anchorNode, DIRECTION_FORWARD);
                if (textNode)
                    anchorNode = textNode;
            }
            if (anchorNode && isContentTextNode(anchorNode)) {
                selection->extend(focusNode, 0, ec);
                if (ec)
                    return String();
            }
        }
    }

    // Enforce that the selection does not cross anchor boundaries. This is
    // a workaround for the asymmetric behavior of WebKit while crossing
    // anchors.
    anchorNode = getImplicitBoundaryNode(selection->anchorNode(),
            selection->anchorOffset(), direction);
    focusNode = getImplicitBoundaryNode(selection->focusNode(),
            selection->focusOffset(), direction);
    if (anchorNode && focusNode && anchorNode != focusNode) {
        Node* inputControl = getIntermediaryInputElement(anchorNode, focusNode,
                direction);
        if (inputControl) {
            if (direction == DIRECTION_FORWARD) {
                if (isDescendantOf(inputControl, anchorNode)) {
                    focusNode = inputControl;
                } else {
                    focusNode = inputControl->traversePreviousSiblingPostOrder(
                            body);
                    if (!focusNode)
                        focusNode = inputControl;
                }
                // We prefer a text node contained in the input element.
                if (!isContentTextNode(focusNode)) {
                    Node* textNode = traverseNextContentTextNode(focusNode,
                        anchorNode, DIRECTION_BACKWARD);
                    if (textNode)
                        focusNode = textNode;
                }
                // If we found text in the input select it.
                // Otherwise, select the input element itself.
                if (isContentTextNode(focusNode)) {
                    selection->extend(focusNode, caretMaxOffset(focusNode), ec);
                } else if (anchorNode != focusNode) {
                    // Note that the focusNode always has parent and that
                    // the offset can be one more that the index of the last
                    // element - this is how WebKit selects such elements.
                    selection->extend(focusNode->parentNode(),
                            focusNode->nodeIndex() + 1, ec);
                }
                if (ec)
                    return String();
            } else {
                if (isDescendantOf(inputControl, anchorNode)) {
                    focusNode = inputControl;
                } else {
                    focusNode = inputControl->traverseNextSibling();
                    if (!focusNode)
                        focusNode = inputControl;
                }
                // We prefer a text node contained in the input element.
                if (!isContentTextNode(focusNode)) {
                    Node* textNode = traverseNextContentTextNode(focusNode,
                            anchorNode, DIRECTION_FORWARD);
                    if (textNode)
                        focusNode = textNode;
                }
                // If we found text in the input select it.
                // Otherwise, select the input element itself.
                if (isContentTextNode(focusNode)) {
                    selection->extend(focusNode, caretMinOffset(focusNode), ec);
                } else if (anchorNode != focusNode) {
                    // Note that the focusNode always has parent and that
                    // the offset can be one more that the index of the last
                    // element - this is how WebKit selects such elements.
                    selection->extend(focusNode->parentNode(),
                            focusNode->nodeIndex() + 1, ec);
                }
                if (ec)
                   return String();
            }
        }
    }

    // make sure the selection is visible
    if (direction == DIRECTION_FORWARD)
        scrollNodeIntoView(m_mainFrame, selection->focusNode());
    else
        scrollNodeIntoView(m_mainFrame, selection->anchorNode());

    // format markup for the visible content
    RefPtr<Range> range = selection->getRangeAt(0, ec);
    if (ec)
        return String();
    IntRect bounds = range->boundingBox();
    selectAt(bounds.center().x(), bounds.center().y());
    markup = formatMarkup(selection);
    ALOGV("Selection markup: %s", markup.utf8().data());

    return markup;
}

Node* WebViewCore::getImplicitBoundaryNode(Node* node, unsigned offset, int direction)
{
    if (node->offsetInCharacters())
        return node;
    if (!node->hasChildNodes())
        return node;
    if (offset < node->childNodeCount())
        return node->childNode(offset);
    else
        if (direction == DIRECTION_FORWARD)
            return node->traverseNextSibling();
        else
            return node->traversePreviousNodePostOrder(
                    node->document()->body());
}

Node* WebViewCore::getNextAnchorNode(Node* anchorNode, bool ignoreFirstNode, int direction)
{
    Node* body = 0;
    Node* currentNode = 0;
    if (direction == DIRECTION_FORWARD) {
        if (ignoreFirstNode)
            currentNode = anchorNode->traverseNextNode(body);
        else
            currentNode = anchorNode;
    } else {
        body = anchorNode->document()->body();
        if (ignoreFirstNode)
            currentNode = anchorNode->traversePreviousSiblingPostOrder(body);
        else
            currentNode = anchorNode;
    }
    while (currentNode) {
        if (isContentTextNode(currentNode)
                || isContentInputElement(currentNode))
            return currentNode;
        if (direction == DIRECTION_FORWARD)
            currentNode = currentNode->traverseNextNode();
        else
            currentNode = currentNode->traversePreviousNodePostOrder(body);
    }
    return 0;
}

void WebViewCore::advanceAnchorNode(DOMSelection* selection, int direction,
        String& markup, bool ignoreFirstNode, ExceptionCode& ec)
{
    Node* anchorNode = getImplicitBoundaryNode(selection->anchorNode(),
            selection->anchorOffset(), direction);
    if (!anchorNode) {
        ec = NOT_FOUND_ERR;
        return;
    }
    // If the anchor offset is invalid i.e. the anchor node has no
    // child with that index getImplicitAnchorNode returns the next
    // logical node in the current direction. In such a case our
    // position in the DOM tree was has already been advanced,
    // therefore we there is no need to do that again.
    if (selection->anchorNode()->isElementNode()) {
        unsigned anchorOffset = selection->anchorOffset();
        unsigned childNodeCount = selection->anchorNode()->childNodeCount();
        if (anchorOffset >= childNodeCount)
            ignoreFirstNode = false;
    }
    // Find the next anchor node given our position in the DOM and
    // whether we want the current node to be considered as well.
    Node* nextAnchorNode = getNextAnchorNode(anchorNode, ignoreFirstNode,
            direction);
    if (!nextAnchorNode) {
        ec = NOT_FOUND_ERR;
        return;
    }
    if (nextAnchorNode->isElementNode()) {
        // If this is an input element tell the WebView thread
        // to set the cursor to that control.
        if (isContentInputElement(nextAnchorNode)) {
            IntRect bounds = nextAnchorNode->getRect();
            selectAt(bounds.center().x(), bounds.center().y());
        }
        Node* textNode = 0;
        // Treat the text content of links as any other text but
        // for the rest input elements select the control itself.
        if (nextAnchorNode->hasTagName(WebCore::HTMLNames::aTag))
            textNode = traverseNextContentTextNode(nextAnchorNode,
                    nextAnchorNode, direction);
        // We prefer to select the text content of the link if such,
        // otherwise just select the element itself.
        if (textNode) {
            nextAnchorNode = textNode;
        } else {
            if (direction == DIRECTION_FORWARD) {
                selection->setBaseAndExtent(nextAnchorNode,
                        caretMinOffset(nextAnchorNode), nextAnchorNode,
                        caretMaxOffset(nextAnchorNode), ec);
            } else {
                selection->setBaseAndExtent(nextAnchorNode,
                        caretMaxOffset(nextAnchorNode), nextAnchorNode,
                        caretMinOffset(nextAnchorNode), ec);
            }
            if (!ec)
                markup = formatMarkup(selection);
            // make sure the selection is visible
            scrollNodeIntoView(selection->frame(), nextAnchorNode);
            return;
        }
    }
    if (direction == DIRECTION_FORWARD)
        selection->setPosition(nextAnchorNode,
                caretMinOffset(nextAnchorNode), ec);
    else
        selection->setPosition(nextAnchorNode,
                caretMaxOffset(nextAnchorNode), ec);
}

bool WebViewCore::isContentInputElement(Node* node)
{
  return (isVisible(node)
          && (node->hasTagName(WebCore::HTMLNames::selectTag)
          || node->hasTagName(WebCore::HTMLNames::aTag)
          || node->hasTagName(WebCore::HTMLNames::inputTag)
          || node->hasTagName(WebCore::HTMLNames::buttonTag)));
}

bool WebViewCore::isContentTextNode(Node* node)
{
   if (!node || !node->isTextNode())
       return false;
   Text* textNode = static_cast<Text*>(node);
   return (isVisible(textNode) && textNode->length() > 0
       && !textNode->containsOnlyWhitespace());
}

Text* WebViewCore::traverseNextContentTextNode(Node* fromNode, Node* toNode, int direction)
{
    Node* currentNode = fromNode;
    do {
        if (direction == DIRECTION_FORWARD)
            currentNode = currentNode->traverseNextNode(toNode);
        else
            currentNode = currentNode->traversePreviousNodePostOrder(toNode);
    } while (currentNode && !isContentTextNode(currentNode));
    return static_cast<Text*>(currentNode);
}

Node* WebViewCore::getIntermediaryInputElement(Node* fromNode, Node* toNode, int direction)
{
    if (fromNode == toNode)
        return 0;
    if (direction == DIRECTION_FORWARD) {
        Node* currentNode = fromNode;
        while (currentNode && currentNode != toNode) {
            if (isContentInputElement(currentNode))
                return currentNode;
            currentNode = currentNode->traverseNextNodePostOrder();
        }
        currentNode = fromNode;
        while (currentNode && currentNode != toNode) {
            if (isContentInputElement(currentNode))
                return currentNode;
            currentNode = currentNode->traverseNextNode();
        }
    } else {
        Node* currentNode = fromNode->traversePreviousNode();
        while (currentNode && currentNode != toNode) {
            if (isContentInputElement(currentNode))
                return currentNode;
            currentNode = currentNode->traversePreviousNode();
        }
        currentNode = fromNode->traversePreviousNodePostOrder();
        while (currentNode && currentNode != toNode) {
            if (isContentInputElement(currentNode))
                return currentNode;
            currentNode = currentNode->traversePreviousNodePostOrder();
        }
    }
    return 0;
}

bool WebViewCore::isDescendantOf(Node* parent, Node* node)
{
    Node* currentNode = node;
    while (currentNode) {
        if (currentNode == parent) {
            return true;
        }
        currentNode = currentNode->parentNode();
    }
    return false;
}

String WebViewCore::modifySelectionDomNavigationAxis(DOMSelection* selection, int direction, int axis)
{
    HTMLElement* body = m_mainFrame->document()->body();
    if (!m_currentNodeDomNavigationAxis && selection->focusNode()) {
        m_currentNodeDomNavigationAxis = selection->focusNode();
        selection->empty();
        if (m_currentNodeDomNavigationAxis->isTextNode())
            m_currentNodeDomNavigationAxis =
                m_currentNodeDomNavigationAxis->parentNode();
    }
    if (!m_currentNodeDomNavigationAxis)
        m_currentNodeDomNavigationAxis = currentFocus();
    if (!m_currentNodeDomNavigationAxis
            || !validNode(m_mainFrame, m_mainFrame,
                                        m_currentNodeDomNavigationAxis))
        m_currentNodeDomNavigationAxis = body;
    Node* currentNode = m_currentNodeDomNavigationAxis;
    if (axis == AXIS_HEADING) {
        if (currentNode == body && direction == DIRECTION_BACKWARD)
            currentNode = currentNode->lastDescendant();
        do {
            if (direction == DIRECTION_FORWARD)
                currentNode = currentNode->traverseNextNode(body);
            else
                currentNode = currentNode->traversePreviousNode(body);
        } while (currentNode && (currentNode->isTextNode()
            || !isVisible(currentNode) || !isHeading(currentNode)));
    } else if (axis == AXIS_PARENT_FIRST_CHILD) {
        if (direction == DIRECTION_FORWARD) {
            currentNode = currentNode->firstChild();
            while (currentNode && (currentNode->isTextNode()
                    || !isVisible(currentNode)))
                currentNode = currentNode->nextSibling();
        } else {
            do {
                if (currentNode == body)
                    return String();
                currentNode = currentNode->parentNode();
            } while (currentNode && (currentNode->isTextNode()
                    || !isVisible(currentNode)));
        }
    } else if (axis == AXIS_SIBLING) {
        do {
            if (direction == DIRECTION_FORWARD)
                currentNode = currentNode->nextSibling();
            else {
                if (currentNode == body)
                    return String();
                currentNode = currentNode->previousSibling();
            }
        } while (currentNode && (currentNode->isTextNode()
                || !isVisible(currentNode)));
    } else if (axis == AXIS_DOCUMENT) {
        currentNode = body;
        if (direction == DIRECTION_FORWARD)
            currentNode = currentNode->lastDescendant();
    } else {
        ALOGE("Invalid axis: %d", axis);
        return String();
    }
    if (currentNode) {
        m_currentNodeDomNavigationAxis = currentNode;
        scrollNodeIntoView(m_mainFrame, currentNode);
        String selectionString = createMarkup(currentNode);
        ALOGV("Selection markup: %s", selectionString.utf8().data());
        return selectionString;
    }
    return String();
}

bool WebViewCore::isHeading(Node* node)
{
    if (node->hasTagName(WebCore::HTMLNames::h1Tag)
            || node->hasTagName(WebCore::HTMLNames::h2Tag)
            || node->hasTagName(WebCore::HTMLNames::h3Tag)
            || node->hasTagName(WebCore::HTMLNames::h4Tag)
            || node->hasTagName(WebCore::HTMLNames::h5Tag)
            || node->hasTagName(WebCore::HTMLNames::h6Tag)) {
        return true;
    }

    if (node->isElementNode()) {
        Element* element = static_cast<Element*>(node);
        String roleAttribute =
            element->getAttribute(WebCore::HTMLNames::roleAttr).string();
        if (equalIgnoringCase(roleAttribute, "heading"))
            return true;
    }

    return false;
}

bool WebViewCore::isVisible(Node* node)
{
    // start off an element
    Element* element = 0;
    if (node->isElementNode())
        element = static_cast<Element*>(node);
    else
        element = node->parentElement();
    // check renderer
    if (!element->renderer()) {
        return false;
    }
    // check size
    if (element->offsetHeight() == 0 || element->offsetWidth() == 0) {
        return false;
    }
    // check style
    Node* body = m_mainFrame->document()->body();
    Node* currentNode = element;
    while (currentNode && currentNode != body) {
        RenderStyle* style = currentNode->computedStyle();
        if (style &&
                (style->display() == WebCore::NONE || style->visibility() == WebCore::HIDDEN)) {
            return false;
        }
        currentNode = currentNode->parentNode();
    }
    return true;
}

String WebViewCore::formatMarkup(DOMSelection* selection)
{
    ExceptionCode ec = 0;
    String markup = String();
    RefPtr<Range> wholeRange = selection->getRangeAt(0, ec);
    if (ec)
        return String();
    if (!wholeRange->startContainer() || !wholeRange->startContainer())
        return String();
    // Since formatted markup contains invisible nodes it
    // is created from the concatenation of the visible fragments.
    Node* firstNode = wholeRange->firstNode();
    Node* pastLastNode = wholeRange->pastLastNode();
    Node* currentNode = firstNode;
    RefPtr<Range> currentRange;

    while (currentNode != pastLastNode) {
        Node* nextNode = currentNode->traverseNextNode();
        if (!isVisible(currentNode)) {
            if (currentRange) {
                markup = markup + currentRange->toHTML().utf8().data();
                currentRange = 0;
            }
        } else {
            if (!currentRange) {
                currentRange = selection->frame()->document()->createRange();
                if (ec)
                    break;
                if (currentNode == firstNode) {
                    currentRange->setStart(wholeRange->startContainer(),
                        wholeRange->startOffset(), ec);
                    if (ec)
                        break;
                } else {
                    currentRange->setStart(currentNode->parentNode(),
                        currentNode->nodeIndex(), ec);
                    if (ec)
                       break;
                }
            }
            if (nextNode == pastLastNode) {
                currentRange->setEnd(wholeRange->endContainer(),
                    wholeRange->endOffset(), ec);
                if (ec)
                    break;
                markup = markup + currentRange->toHTML().utf8().data();
            } else {
                if (currentNode->offsetInCharacters())
                    currentRange->setEnd(currentNode,
                        currentNode->maxCharacterOffset(), ec);
                else
                    currentRange->setEnd(currentNode->parentNode(),
                            currentNode->nodeIndex() + 1, ec);
                if (ec)
                    break;
            }
        }
        currentNode = nextNode;
    }
    return markup.stripWhiteSpace();
}

void WebViewCore::selectAt(int x, int y)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_selectAt, x, y);
    checkException(env);
}

void WebViewCore::deleteSelection(int start, int end, int textGeneration)
{
    setSelection(start, end);
    if (start == end)
        return;
    WebCore::Node* focus = currentFocus();
    if (!focus)
        return;
    // Prevent our editor client from passing a message to change the
    // selection.
    EditorClientAndroid* client = static_cast<EditorClientAndroid*>(
            m_mainFrame->editor()->client());
    client->setUiGeneratedSelectionChange(true);
    PlatformKeyboardEvent down(AKEYCODE_DEL, 0, 0, true, false, false, false);
    PlatformKeyboardEvent up(AKEYCODE_DEL, 0, 0, false, false, false, false);
    key(down);
    key(up);
    client->setUiGeneratedSelectionChange(false);
    m_textGeneration = textGeneration;
}

void WebViewCore::replaceTextfieldText(int oldStart,
        int oldEnd, const WTF::String& replace, int start, int end,
        int textGeneration)
{
    WebCore::Node* focus = currentFocus();
    if (!focus)
        return;
    setSelection(oldStart, oldEnd);
    // Prevent our editor client from passing a message to change the
    // selection.
    EditorClientAndroid* client = static_cast<EditorClientAndroid*>(
            m_mainFrame->editor()->client());
    client->setUiGeneratedSelectionChange(true);
    if (replace.length())
        WebCore::TypingCommand::insertText(focus->document(), replace,
                false);
    else
        WebCore::TypingCommand::deleteSelection(focus->document());
    client->setUiGeneratedSelectionChange(false);
    // setSelection calls revealSelection, so there is no need to do it here.
    setSelection(start, end);
    m_textGeneration = textGeneration;
}

void WebViewCore::passToJs(int generation, const WTF::String& current,
    const PlatformKeyboardEvent& event)
{
    WebCore::Node* focus = currentFocus();
    if (!focus) {
        clearTextEntry();
        return;
    }
    // Block text field updates during a key press.
    m_blockTextfieldUpdates = true;
    // Also prevent our editor client from passing a message to change the
    // selection.
    EditorClientAndroid* client = static_cast<EditorClientAndroid*>(
            m_mainFrame->editor()->client());
    client->setUiGeneratedSelectionChange(true);
    key(event);
    client->setUiGeneratedSelectionChange(false);
    m_blockTextfieldUpdates = false;
    m_textGeneration = generation;
    WTF::String test = getInputText(focus);
    if (test != current) {
        // If the text changed during the key event, update the UI text field.
        updateTextfield(focus, false, test);
    }
    // Now that the selection has settled down, send it.
    updateTextSelection();
}

void WebViewCore::scrollFocusedTextInput(float xPercent, int y)
{
    WebCore::Node* focus = currentFocus();
    if (!focus) {
        clearTextEntry();
        return;
    }
    WebCore::RenderTextControl* renderText = toRenderTextControl(focus);
    if (!renderText) {
        clearTextEntry();
        return;
    }

    int x = (int) (xPercent * (renderText->scrollWidth() -
        renderText->clientWidth()));
    renderText->setScrollLeft(x);
    renderText->setScrollTop(y);
}

void WebViewCore::setFocusControllerActive(bool active)
{
    m_mainFrame->page()->focusController()->setActive(active);
}

void WebViewCore::saveDocumentState(WebCore::Frame* frame)
{
    if (!validNode(m_mainFrame, frame, 0))
        frame = m_mainFrame;
    WebCore::HistoryItem *item = frame->loader()->history()->currentItem();

    // item can be null when there is no offical URL for the current page. This happens
    // when the content is loaded using with WebCoreFrameBridge::LoadData() and there
    // is no failing URL (common case is when content is loaded using data: scheme)
    if (item) {
        item->setDocumentState(frame->document()->formElementsState());
    }
}

// Create an array of java Strings.
static jobjectArray makeLabelArray(JNIEnv* env, const uint16_t** labels, size_t count)
{
    jclass stringClass = env->FindClass("java/lang/String");
    ALOG_ASSERT(stringClass, "Could not find java/lang/String");
    jobjectArray array = env->NewObjectArray(count, stringClass, 0);
    ALOG_ASSERT(array, "Could not create new string array");

    for (size_t i = 0; i < count; i++) {
        jobject newString = env->NewString(&labels[i][1], labels[i][0]);
        env->SetObjectArrayElement(array, i, newString);
        env->DeleteLocalRef(newString);
        checkException(env);
    }
    env->DeleteLocalRef(stringClass);
    return array;
}

void WebViewCore::openFileChooser(PassRefPtr<WebCore::FileChooser> chooser)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;

    if (!chooser)
        return;

    WTF::String acceptType = chooser->acceptTypes();
    jstring jAcceptType = wtfStringToJstring(env, acceptType, true);
    jstring jName = (jstring) env->CallObjectMethod(
            javaObject.get(), m_javaGlue->m_openFileChooser, jAcceptType);
    checkException(env);
    env->DeleteLocalRef(jAcceptType);

    WTF::String wtfString = jstringToWtfString(env, jName);
    env->DeleteLocalRef(jName);

    if (!wtfString.isEmpty())
        chooser->chooseFile(wtfString);
}

void WebViewCore::listBoxRequest(WebCoreReply* reply, const uint16_t** labels, size_t count, const int enabled[], size_t enabledCount,
        bool multiple, const int selected[], size_t selectedCountOrSelection)
{
    ALOG_ASSERT(m_javaGlue->m_obj, "No java widget associated with this view!");

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;

    // If m_popupReply is not null, then we already have a list showing.
    if (m_popupReply != 0)
        return;

    // Create an array of java Strings for the drop down.
    jobjectArray labelArray = makeLabelArray(env, labels, count);

    // Create an array determining whether each item is enabled.
    jintArray enabledArray = env->NewIntArray(enabledCount);
    checkException(env);
    jint* ptrArray = env->GetIntArrayElements(enabledArray, 0);
    checkException(env);
    for (size_t i = 0; i < enabledCount; i++) {
        ptrArray[i] = enabled[i];
    }
    env->ReleaseIntArrayElements(enabledArray, ptrArray, 0);
    checkException(env);

    if (multiple) {
        // Pass up an array representing which items are selected.
        jintArray selectedArray = env->NewIntArray(selectedCountOrSelection);
        checkException(env);
        jint* selArray = env->GetIntArrayElements(selectedArray, 0);
        checkException(env);
        for (size_t i = 0; i < selectedCountOrSelection; i++) {
            selArray[i] = selected[i];
        }
        env->ReleaseIntArrayElements(selectedArray, selArray, 0);

        env->CallVoidMethod(javaObject.get(),
                m_javaGlue->m_requestListBox, labelArray, enabledArray,
                selectedArray);
        env->DeleteLocalRef(selectedArray);
    } else {
        // Pass up the single selection.
        env->CallVoidMethod(javaObject.get(),
                m_javaGlue->m_requestSingleListBox, labelArray, enabledArray,
                selectedCountOrSelection);
    }

    env->DeleteLocalRef(labelArray);
    env->DeleteLocalRef(enabledArray);
    checkException(env);

    Retain(reply);
    m_popupReply = reply;
}

bool WebViewCore::key(const PlatformKeyboardEvent& event)
{
    WebCore::EventHandler* eventHandler;
    WebCore::Node* focusNode = currentFocus();
    if (focusNode) {
        WebCore::Frame* frame = focusNode->document()->frame();
        eventHandler = frame->eventHandler();
        VisibleSelection old = frame->selection()->selection();
        EditorClientAndroid* client = static_cast<EditorClientAndroid*>(
                m_mainFrame->editor()->client());
        client->setUiGeneratedSelectionChange(true);
        bool handled = eventHandler->keyEvent(event);
        client->setUiGeneratedSelectionChange(false);
        if (isContentEditable(focusNode)) {
            // keyEvent will return true even if the contentEditable did not
            // change its selection.  In the case that it does not, we want to
            // return false so that the key will be sent back to our navigation
            // system.
            handled |= frame->selection()->selection() != old;
        }
        return handled;
    } else {
        eventHandler = focusedFrame()->eventHandler();
    }
    return eventHandler->keyEvent(event);
}

// For when the user clicks the trackball, presses dpad center, or types into an
// unfocused textfield.  In the latter case, 'fake' will be true
void WebViewCore::click(WebCore::Frame* frame, WebCore::Node* node, bool fake) {
    if (!node) {
        WebCore::IntPoint pt = m_mousePos;
        pt.move(m_scrollOffsetX, m_scrollOffsetY);
        WebCore::HitTestResult hitTestResult = m_mainFrame->eventHandler()->
                hitTestResultAtPoint(pt, false);
        node = hitTestResult.innerNode();
        frame = node->document()->frame();
    }
    if (node) {
        EditorClientAndroid* client
                = static_cast<EditorClientAndroid*>(
                m_mainFrame->editor()->client());
        client->setShouldChangeSelectedRange(false);
        handleMouseClick(frame, node, fake);
        client->setShouldChangeSelectedRange(true);
    }
}

#if USE(ACCELERATED_COMPOSITING)
GraphicsLayerAndroid* WebViewCore::graphicsRootLayer() const
{
    RenderView* contentRenderer = m_mainFrame->contentRenderer();
    if (!contentRenderer)
        return 0;
    return static_cast<GraphicsLayerAndroid*>(
          contentRenderer->compositor()->rootPlatformLayer());
}
#endif

bool WebViewCore::handleTouchEvent(int action, Vector<int>& ids, Vector<IntPoint>& points, int actionIndex, int metaState)
{
    bool preventDefault = false;

#if USE(ACCELERATED_COMPOSITING)
    GraphicsLayerAndroid* rootLayer = graphicsRootLayer();
    if (rootLayer)
      rootLayer->pauseDisplay(true);
#endif

#if ENABLE(TOUCH_EVENTS) // Android
    #define MOTION_EVENT_ACTION_POINTER_DOWN 5
    #define MOTION_EVENT_ACTION_POINTER_UP 6

    WebCore::TouchEventType type = WebCore::TouchStart;
    WebCore::PlatformTouchPoint::State defaultTouchState;
    Vector<WebCore::PlatformTouchPoint::State> touchStates(points.size());

    switch (action) {
    case 0: // MotionEvent.ACTION_DOWN
        type = WebCore::TouchStart;
        defaultTouchState = WebCore::PlatformTouchPoint::TouchPressed;
        break;
    case 1: // MotionEvent.ACTION_UP
        type = WebCore::TouchEnd;
        defaultTouchState = WebCore::PlatformTouchPoint::TouchReleased;
        break;
    case 2: // MotionEvent.ACTION_MOVE
        type = WebCore::TouchMove;
        defaultTouchState = WebCore::PlatformTouchPoint::TouchMoved;
        break;
    case 3: // MotionEvent.ACTION_CANCEL
        type = WebCore::TouchCancel;
        defaultTouchState = WebCore::PlatformTouchPoint::TouchCancelled;
        break;
    case 5: // MotionEvent.ACTION_POINTER_DOWN
        type = WebCore::TouchStart;
        defaultTouchState = WebCore::PlatformTouchPoint::TouchStationary;
        break;
    case 6: // MotionEvent.ACTION_POINTER_UP
        type = WebCore::TouchEnd;
        defaultTouchState = WebCore::PlatformTouchPoint::TouchStationary;
        break;
    case 0x100: // WebViewCore.ACTION_LONGPRESS
        type = WebCore::TouchLongPress;
        defaultTouchState = WebCore::PlatformTouchPoint::TouchPressed;
        break;
    case 0x200: // WebViewCore.ACTION_DOUBLETAP
        type = WebCore::TouchDoubleTap;
        defaultTouchState = WebCore::PlatformTouchPoint::TouchPressed;
        break;
    default:
        // We do not support other kinds of touch event inside WebCore
        // at the moment.
        ALOGW("Java passed a touch event type that we do not support in WebCore: %d", action);
        return 0;
    }

    for (int c = 0; c < static_cast<int>(points.size()); c++) {
        points[c].setX(points[c].x() - m_scrollOffsetX);
        points[c].setY(points[c].y() - m_scrollOffsetY);

        // Setting the touch state for each point.
        // Note: actionIndex will be 0 for all actions that are not ACTION_POINTER_DOWN/UP.
        if (action == MOTION_EVENT_ACTION_POINTER_DOWN && c == actionIndex) {
            touchStates[c] = WebCore::PlatformTouchPoint::TouchPressed;
        } else if (action == MOTION_EVENT_ACTION_POINTER_UP && c == actionIndex) {
            touchStates[c] = WebCore::PlatformTouchPoint::TouchReleased;
        } else {
            touchStates[c] = defaultTouchState;
        };
    }

    WebCore::PlatformTouchEvent te(ids, points, type, touchStates, metaState);
    preventDefault = m_mainFrame->eventHandler()->handleTouchEvent(te);
#endif

#if USE(ACCELERATED_COMPOSITING)
    if (rootLayer)
      rootLayer->pauseDisplay(false);
#endif
    return preventDefault;
}

void WebViewCore::touchUp(int touchGeneration,
    WebCore::Frame* frame, WebCore::Node* node, int x, int y)
{
    if (touchGeneration == 0) {
        // m_mousePos should be set in getTouchHighlightRects()
        WebCore::HitTestResult hitTestResult = m_mainFrame->eventHandler()->hitTestResultAtPoint(m_mousePos, false);
        node = hitTestResult.innerNode();
        if (node)
            frame = node->document()->frame();
        else
            frame = 0;
    } else {
        if (m_touchGeneration > touchGeneration)
            return; // short circuit if a newer touch has been generated
        // This moves m_mousePos to the correct place, and handleMouseClick uses
        // m_mousePos to determine where the click happens.
        moveMouse(frame, x, y);
        m_lastGeneration = touchGeneration;
    }
    if (frame && validNode(m_mainFrame, frame, 0)) {
        frame->loader()->resetMultipleFormSubmissionProtection();
    }
    handleMouseClick(frame, node, false);
}

// Check for the "x-webkit-soft-keyboard" attribute.  If it is there and
// set to hidden, do not show the soft keyboard.  Node passed as a parameter
// must not be null.
static bool shouldSuppressKeyboard(const WebCore::Node* node) {
    ALOG_ASSERT(node, "node passed to shouldSuppressKeyboard cannot be null");
    const NamedNodeMap* attributes = node->attributes();
    if (!attributes) return false;
    size_t length = attributes->length();
    for (size_t i = 0; i < length; i++) {
        const Attribute* a = attributes->attributeItem(i);
        if (a->localName() == "x-webkit-soft-keyboard" && a->value() == "hidden")
            return true;
    }
    return false;
}

// Common code for both clicking with the trackball and touchUp
// Also used when typing into a non-focused textfield to give the textfield focus,
// in which case, 'fake' is set to true
bool WebViewCore::handleMouseClick(WebCore::Frame* framePtr, WebCore::Node* nodePtr, bool fake)
{
    bool valid = !framePtr || validNode(m_mainFrame, framePtr, nodePtr);
    WebFrame* webFrame = WebFrame::getWebFrame(m_mainFrame);
    if (valid && nodePtr) {
    // Need to special case area tags because an image map could have an area element in the middle
    // so when attempting to get the default, the point chosen would be follow the wrong link.
        if (nodePtr->hasTagName(WebCore::HTMLNames::areaTag)) {
            webFrame->setUserInitiatedAction(true);
            nodePtr->dispatchSimulatedClick(0, true, true);
            webFrame->setUserInitiatedAction(false);
            return true;
        }
    }
    if (!valid || !framePtr)
        framePtr = m_mainFrame;
    webFrame->setUserInitiatedAction(true);
    WebCore::PlatformMouseEvent mouseDown(m_mousePos, m_mousePos, WebCore::LeftButton,
            WebCore::MouseEventPressed, 1, false, false, false, false,
            WTF::currentTime());
    // ignore the return from as it will return true if the hit point can trigger selection change
    framePtr->eventHandler()->handleMousePressEvent(mouseDown);
    WebCore::PlatformMouseEvent mouseUp(m_mousePos, m_mousePos, WebCore::LeftButton,
            WebCore::MouseEventReleased, 1, false, false, false, false,
            WTF::currentTime());
    bool handled = framePtr->eventHandler()->handleMouseReleaseEvent(mouseUp);
    webFrame->setUserInitiatedAction(false);

    // If the user clicked on a textfield, make the focusController active
    // so we show the blinking cursor.
    WebCore::Node* focusNode = currentFocus();
    if (focusNode) {
        WebCore::RenderTextControl* rtc = toRenderTextControl(focusNode);
        if (rtc) {
            bool ime = !shouldSuppressKeyboard(focusNode)
                    && !(static_cast<WebCore::HTMLInputElement*>(focusNode))->readOnly();
            if (ime) {
#if ENABLE(WEB_AUTOFILL)
                if (rtc->isTextField()) {
                    EditorClientAndroid* editorC = static_cast<EditorClientAndroid*>(framePtr->page()->editorClient());
                    WebAutofill* autoFill = editorC->getAutofill();
                    autoFill->formFieldFocused(static_cast<HTMLFormControlElement*>(focusNode));
                }
#endif
                if (!fake)
                    initEditField(focusNode);
            } else if (!fake) {
                requestKeyboard(false);
            }
        } else if (!fake){
            // If the selection is contentEditable, show the keyboard so the
            // user can type.  Otherwise hide the keyboard because no text
            // input is needed.
            if (isContentEditable(focusNode)) {
                initEditField(focusNode);
            } else if (!nodeIsPlugin(focusNode)) {
                clearTextEntry();
            }
        }
    } else if (!fake) {
        // There is no focusNode, so the keyboard is not needed.
        clearTextEntry();
    }
    return handled;
}

WebViewCore::InputType WebViewCore::getInputType(Node* node)
{
    WebCore::RenderObject* renderer = node->renderer();
    if (!renderer)
        return WebViewCore::NONE;
    if (renderer->isTextArea())
        return WebViewCore::TEXT_AREA;

    if (node->hasTagName(WebCore::HTMLNames::inputTag)) {
        HTMLInputElement* htmlInput = static_cast<HTMLInputElement*>(node);
        if (htmlInput->isPasswordField())
            return WebViewCore::PASSWORD;
        if (htmlInput->isSearchField())
            return WebViewCore::SEARCH;
        if (htmlInput->isEmailField())
            return WebViewCore::EMAIL;
        if (htmlInput->isNumberField())
            return WebViewCore::NUMBER;
        if (htmlInput->isTelephoneField())
            return WebViewCore::TELEPHONE;
        if (htmlInput->isTextField())
            return WebViewCore::NORMAL_TEXT_FIELD;
    }

    if (node->isContentEditable())
        return WebViewCore::TEXT_AREA;

    return WebViewCore::NONE;
}

int WebViewCore::getMaxLength(Node* node)
{
    int maxLength = -1;
    if (node->hasTagName(WebCore::HTMLNames::inputTag)) {
        HTMLInputElement* htmlInput = static_cast<HTMLInputElement*>(node);
        maxLength = htmlInput->maxLength();
    }
    return maxLength;
}

bool WebViewCore::isSpellCheckEnabled(Node* node)
{
    bool isEnabled = true;
    if (node->isElementNode()) {
        WebCore::Element* element = static_cast<WebCore::Element*>(node);
        isEnabled = element->isSpellCheckingEnabled();
    }
    return isEnabled;
}

void WebViewCore::initEditField(Node* node)
{
    String text = getInputText(node);
    int start = 0;
    int end = 0;
    getSelectionOffsets(node, start, end);
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    m_textGeneration = 0;
    InputType inputType = getInputType(node);
    Document* document = node->document();
    PlatformKeyboardEvent tab(AKEYCODE_TAB, 0, 0, false, false, false, false);
    PassRefPtr<KeyboardEvent> tabEvent =
            KeyboardEvent::create(tab, document->defaultView());
    Node* nextFocus = document->nextFocusableNode(node, tabEvent.get());
    bool isNextText = isTextInput(nextFocus);
    bool spellCheckEnabled = isSpellCheckEnabled(node);
    int maxLength = getMaxLength(node);
    String label = requestLabel(document->frame(), node);
    jstring fieldText = wtfStringToJstring(env, text, true);
    jstring labelText = wtfStringToJstring(env, text, false);
    SelectText* selectText = createSelectText(focusedFrame()->selection()->selection());
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_initEditField,
            reinterpret_cast<int>(node), fieldText, inputType,
            spellCheckEnabled, isNextText, labelText, start, end,
            reinterpret_cast<int>(selectText), maxLength);
    checkException(env);
}

void WebViewCore::popupReply(int index)
{
    if (m_popupReply) {
        m_popupReply->replyInt(index);
        Release(m_popupReply);
        m_popupReply = 0;
    }
}

void WebViewCore::popupReply(const int* array, int count)
{
    if (m_popupReply) {
        m_popupReply->replyIntArray(array, count);
        Release(m_popupReply);
        m_popupReply = 0;
    }
}

void WebViewCore::formDidBlur(const WebCore::Node* node)
{
    // If the blur is on a text input, keep track of the node so we can
    // hide the soft keyboard when the new focus is set, if it is not a
    // text input.
    if (isTextInput(node))
        m_blurringNodePointer = reinterpret_cast<int>(node);
}

// This is a slightly modified Node::nextNodeConsideringAtomicNodes() with the
// extra constraint of limiting the search to inside a containing parent
WebCore::Node* nextNodeWithinParent(WebCore::Node* parent, WebCore::Node* start)
{
    if (!isAtomicNode(start) && start->firstChild())
        return start->firstChild();
    if (start->nextSibling())
        return start->nextSibling();
    const Node *n = start;
    while (n && !n->nextSibling()) {
        n = n->parentNode();
        if (n == parent)
            return 0;
    }
    if (n)
        return n->nextSibling();
    return 0;
}

void WebViewCore::focusNodeChanged(WebCore::Node* newFocus)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    if (!isTextInput(newFocus) && m_blurringNodePointer) {
        env->CallVoidMethod(javaObject.get(), m_javaGlue->m_formDidBlur, m_blurringNodePointer);
        checkException(env);
        m_blurringNodePointer = 0;
    }
    HitTestResult focusHitResult;
    focusHitResult.setInnerNode(newFocus);
    focusHitResult.setInnerNonSharedNode(newFocus);
    if (newFocus && newFocus->isLink() && newFocus->isElementNode()) {
        focusHitResult.setURLElement(static_cast<Element*>(newFocus));
        if (newFocus->hasChildNodes() && !newFocus->hasTagName(HTMLNames::imgTag)) {
            // Check to see if any of the children are images, and if so
            // set them as the innerNode and innerNonSharedNode
            // This will stop when it hits the first image. I'm not sure what
            // should be done in the case of multiple images inside one anchor...
            Node* nextNode = newFocus->firstChild();
            bool found = false;
            while (nextNode) {
                if (nextNode->hasTagName(HTMLNames::imgTag)) {
                    found = true;
                    break;
                }
                nextNode = nextNodeWithinParent(newFocus, nextNode);
            }
            if (found) {
                focusHitResult.setInnerNode(nextNode);
                focusHitResult.setInnerNonSharedNode(nextNode);
            }
        }
    }
    AndroidHitTestResult androidHitTest(this, focusHitResult);
    jobject jHitTestObj = androidHitTest.createJavaObject(env);
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_focusNodeChanged, jHitTestObj);
    env->DeleteLocalRef(jHitTestObj);
}

void WebViewCore::addMessageToConsole(const WTF::String& message, unsigned int lineNumber, const WTF::String& sourceID, int msgLevel) {
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    jstring jMessageStr = wtfStringToJstring(env, message);
    jstring jSourceIDStr = wtfStringToJstring(env, sourceID);
    env->CallVoidMethod(javaObject.get(),
            m_javaGlue->m_addMessageToConsole, jMessageStr, lineNumber,
            jSourceIDStr, msgLevel);
    env->DeleteLocalRef(jMessageStr);
    env->DeleteLocalRef(jSourceIDStr);
    checkException(env);
}

void WebViewCore::jsAlert(const WTF::String& url, const WTF::String& text)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    jstring jInputStr = wtfStringToJstring(env, text);
    jstring jUrlStr = wtfStringToJstring(env, url);
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_jsAlert, jUrlStr, jInputStr);
    env->DeleteLocalRef(jInputStr);
    env->DeleteLocalRef(jUrlStr);
    checkException(env);
}

bool WebViewCore::exceededDatabaseQuota(const WTF::String& url, const WTF::String& databaseIdentifier, const unsigned long long currentQuota, unsigned long long estimatedSize)
{
#if ENABLE(DATABASE)
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return false;
    jstring jDatabaseIdentifierStr = wtfStringToJstring(env, databaseIdentifier);
    jstring jUrlStr = wtfStringToJstring(env, url);
    env->CallVoidMethod(javaObject.get(),
            m_javaGlue->m_exceededDatabaseQuota, jUrlStr,
            jDatabaseIdentifierStr, currentQuota, estimatedSize);
    env->DeleteLocalRef(jDatabaseIdentifierStr);
    env->DeleteLocalRef(jUrlStr);
    checkException(env);
    return true;
#endif
}

bool WebViewCore::reachedMaxAppCacheSize(const unsigned long long spaceNeeded)
{
#if ENABLE(OFFLINE_WEB_APPLICATIONS)
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return false;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_reachedMaxAppCacheSize, spaceNeeded);
    checkException(env);
    return true;
#endif
}

void WebViewCore::populateVisitedLinks(WebCore::PageGroup* group)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    m_groupForVisitedLinks = group;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_populateVisitedLinks);
    checkException(env);
}

void WebViewCore::geolocationPermissionsShowPrompt(const WTF::String& origin)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    jstring originString = wtfStringToJstring(env, origin);
    env->CallVoidMethod(javaObject.get(),
                        m_javaGlue->m_geolocationPermissionsShowPrompt,
                        originString);
    env->DeleteLocalRef(originString);
    checkException(env);
}

void WebViewCore::geolocationPermissionsHidePrompt()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_geolocationPermissionsHidePrompt);
    checkException(env);
}

jobject WebViewCore::getDeviceMotionService()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return 0;
    jobject object = env->CallObjectMethod(javaObject.get(), m_javaGlue->m_getDeviceMotionService);
    checkException(env);
    return object;
}

jobject WebViewCore::getDeviceOrientationService()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return 0;
    jobject object = env->CallObjectMethod(javaObject.get(), m_javaGlue->m_getDeviceOrientationService);
    checkException(env);
    return object;
}

bool WebViewCore::jsConfirm(const WTF::String& url, const WTF::String& text)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return false;
    jstring jInputStr = wtfStringToJstring(env, text);
    jstring jUrlStr = wtfStringToJstring(env, url);
    jboolean result = env->CallBooleanMethod(javaObject.get(), m_javaGlue->m_jsConfirm, jUrlStr, jInputStr);
    env->DeleteLocalRef(jInputStr);
    env->DeleteLocalRef(jUrlStr);
    checkException(env);
    return result;
}

bool WebViewCore::jsPrompt(const WTF::String& url, const WTF::String& text, const WTF::String& defaultValue, WTF::String& result)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return false;
    jstring jUrlStr = wtfStringToJstring(env, url);
    jstring jInputStr = wtfStringToJstring(env, text);
    jstring jDefaultStr = wtfStringToJstring(env, defaultValue);
    jstring returnVal = static_cast<jstring>(env->CallObjectMethod(javaObject.get(), m_javaGlue->m_jsPrompt, jUrlStr, jInputStr, jDefaultStr));
    env->DeleteLocalRef(jUrlStr);
    env->DeleteLocalRef(jInputStr);
    env->DeleteLocalRef(jDefaultStr);
    checkException(env);

    // If returnVal is null, it means that the user cancelled the dialog.
    if (!returnVal)
        return false;

    result = jstringToWtfString(env, returnVal);
    env->DeleteLocalRef(returnVal);
    return true;
}

bool WebViewCore::jsUnload(const WTF::String& url, const WTF::String& message)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return false;
    jstring jInputStr = wtfStringToJstring(env, message);
    jstring jUrlStr = wtfStringToJstring(env, url);
    jboolean result = env->CallBooleanMethod(javaObject.get(), m_javaGlue->m_jsUnload, jUrlStr, jInputStr);
    env->DeleteLocalRef(jInputStr);
    env->DeleteLocalRef(jUrlStr);
    checkException(env);
    return result;
}

bool WebViewCore::jsInterrupt()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return false;
    jboolean result = env->CallBooleanMethod(javaObject.get(), m_javaGlue->m_jsInterrupt);
    checkException(env);
    return result;
}

AutoJObject
WebViewCore::getJavaObject()
{
    return m_javaGlue->object(JSC::Bindings::getJNIEnv());
}

jobject
WebViewCore::getWebViewJavaObject()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return 0;
    return env->GetObjectField(javaObject.get(), gWebViewCoreFields.m_webView);
}

RenderTextControl* WebViewCore::toRenderTextControl(Node* node)
{
    RenderTextControl* rtc = 0;
    RenderObject* renderer = node->renderer();
    if (renderer && renderer->isTextControl()) {
        rtc = WebCore::toRenderTextControl(renderer);
    }
    return rtc;
}

void WebViewCore::getSelectionOffsets(Node* node, int& start, int& end)
{
    RenderTextControl* rtc = toRenderTextControl(node);
    if (rtc) {
        start = rtc->selectionStart();
        end = rtc->selectionEnd();
    } else {
        // It must be content editable field.
        Document* document = node->document();
        Frame* frame = document->frame();
        SelectionController* selector = frame->selection();
        Position selectionStart = selector->start();
        Position selectionEnd = selector->end();
        Position startOfNode = firstPositionInNode(node);
        RefPtr<Range> startRange = Range::create(document, startOfNode,
                selectionStart);
        start = TextIterator::rangeLength(startRange.get(), true);
        RefPtr<Range> endRange = Range::create(document, startOfNode,
                selectionEnd);
        end = TextIterator::rangeLength(endRange.get(), true);
    }
}

String WebViewCore::getInputText(Node* node)
{
    String text;
    WebCore::RenderTextControl* renderText = toRenderTextControl(node);
    if (renderText)
        text = renderText->text();
    else {
        // It must be content editable field.
        Position start = firstPositionInNode(node);
        Position end = lastPositionInNode(node);
        VisibleSelection allEditableText(start, end);
        if (allEditableText.isRange())
            text = allEditableText.firstRange()->text();
    }
    return text;
}

void WebViewCore::updateTextSelection()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    VisibleSelection selection = focusedFrame()->selection()->selection();
    int start = 0;
    int end = 0;
    if (selection.isCaretOrRange())
        getSelectionOffsets(selection.start().anchorNode(), start, end);
    SelectText* selectText = createSelectText(selection);
    env->CallVoidMethod(javaObject.get(),
            m_javaGlue->m_updateTextSelection, reinterpret_cast<int>(currentFocus()),
            start, end, m_textGeneration, reinterpret_cast<int>(selectText));
    checkException(env);
}

void WebViewCore::updateTextfield(WebCore::Node* ptr, bool changeToPassword,
        const WTF::String& text)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    if (m_blockTextfieldUpdates)
        return;
    if (changeToPassword) {
        env->CallVoidMethod(javaObject.get(), m_javaGlue->m_updateTextfield,
                (int) ptr, true, 0, m_textGeneration);
        checkException(env);
        return;
    }
    jstring string = wtfStringToJstring(env, text);
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_updateTextfield,
            (int) ptr, false, string, m_textGeneration);
    env->DeleteLocalRef(string);
    checkException(env);
}

void WebViewCore::clearTextEntry()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_clearTextEntry);
}

void WebViewCore::setBackgroundColor(SkColor c)
{
    WebCore::FrameView* view = m_mainFrame->view();
    if (!view)
        return;

    // need (int) cast to find the right constructor
    WebCore::Color bcolor((int)SkColorGetR(c), (int)SkColorGetG(c),
                          (int)SkColorGetB(c), (int)SkColorGetA(c));
    view->setBaseBackgroundColor(bcolor);

    // Background color of 0 indicates we want a transparent background
    if (c == 0)
        view->setTransparent(true);
}

jclass WebViewCore::getPluginClass(const WTF::String& libName, const char* className)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return 0;

    jstring libString = wtfStringToJstring(env, libName);
    jstring classString = env->NewStringUTF(className);
    jobject pluginClass = env->CallObjectMethod(javaObject.get(),
                                           m_javaGlue->m_getPluginClass,
                                           libString, classString);
    checkException(env);

    // cleanup unneeded local JNI references
    env->DeleteLocalRef(libString);
    env->DeleteLocalRef(classString);

    if (pluginClass != 0) {
        return static_cast<jclass>(pluginClass);
    } else {
        return 0;
    }
}

void WebViewCore::showFullScreenPlugin(jobject childView, int32_t orientation, NPP npp)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;

    env->CallVoidMethod(javaObject.get(),
                        m_javaGlue->m_showFullScreenPlugin,
                        childView, orientation, reinterpret_cast<int>(npp));
    checkException(env);
}

void WebViewCore::hideFullScreenPlugin()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_hideFullScreenPlugin);
    checkException(env);
}

jobject WebViewCore::createSurface(jobject view)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return 0;
    jobject result = env->CallObjectMethod(javaObject.get(), m_javaGlue->m_createSurface, view);
    checkException(env);
    return result;
}

jobject WebViewCore::addSurface(jobject view, int x, int y, int width, int height)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return 0;
    jobject result = env->CallObjectMethod(javaObject.get(),
                                           m_javaGlue->m_addSurface,
                                           view, x, y, width, height);
    checkException(env);
    return result;
}

void WebViewCore::updateSurface(jobject childView, int x, int y, int width, int height)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(),
                        m_javaGlue->m_updateSurface, childView,
                        x, y, width, height);
    checkException(env);
}

void WebViewCore::destroySurface(jobject childView)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_destroySurface, childView);
    checkException(env);
}

jobject WebViewCore::getContext()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return 0;

    jobject result = env->CallObjectMethod(javaObject.get(), m_javaGlue->m_getContext);
    checkException(env);
    return result;
}

void WebViewCore::keepScreenOn(bool screenOn) {
    if ((screenOn && m_screenOnCounter == 0) || (!screenOn && m_screenOnCounter == 1)) {
        JNIEnv* env = JSC::Bindings::getJNIEnv();
        AutoJObject javaObject = m_javaGlue->object(env);
        if (!javaObject.get())
            return;
        env->CallVoidMethod(javaObject.get(), m_javaGlue->m_keepScreenOn, screenOn);
        checkException(env);
    }

    // update the counter
    if (screenOn)
        m_screenOnCounter++;
    else if (m_screenOnCounter > 0)
        m_screenOnCounter--;
}

bool WebViewCore::validNodeAndBounds(Frame* frame, Node* node,
    const IntRect& originalAbsoluteBounds)
{
    bool valid = validNode(m_mainFrame, frame, node);
    if (!valid)
        return false;
    RenderObject* renderer = node->renderer();
    if (!renderer)
        return false;
    IntRect absBounds = node->hasTagName(HTMLNames::areaTag)
        ? getAreaRect(static_cast<HTMLAreaElement*>(node))
        : renderer->absoluteBoundingBoxRect();
    return absBounds == originalAbsoluteBounds;
}

void WebViewCore::showRect(int left, int top, int width, int height,
        int contentWidth, int contentHeight, float xPercentInDoc,
        float xPercentInView, float yPercentInDoc, float yPercentInView)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_showRect,
            left, top, width, height, contentWidth, contentHeight,
            xPercentInDoc, xPercentInView, yPercentInDoc, yPercentInView);
    checkException(env);
}

void WebViewCore::centerFitRect(int x, int y, int width, int height)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_centerFitRect, x, y, width, height);
    checkException(env);
}

void WebViewCore::setScrollbarModes(ScrollbarMode horizontalMode, ScrollbarMode verticalMode)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_setScrollbarModes, horizontalMode, verticalMode);
    checkException(env);
}

void WebViewCore::notifyWebAppCanBeInstalled()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_setInstallableWebApp);
    checkException(env);
}

#if ENABLE(VIDEO)
void WebViewCore::enterFullscreenForVideoLayer(int layerId, const WTF::String& url)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    jstring jUrlStr = wtfStringToJstring(env, url);
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_enterFullscreenForVideoLayer, layerId, jUrlStr);
    m_fullscreenVideoMode = true;
    checkException(env);
}

void WebViewCore::exitFullscreenVideo()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    if (m_fullscreenVideoMode) {
        env->CallVoidMethod(javaObject.get(), m_javaGlue->m_exitFullscreenVideo);
        m_fullscreenVideoMode = false;
    }
    checkException(env);
}
#endif

void WebViewCore::setWebTextViewAutoFillable(int queryId, const string16& previewSummary)
{
#if ENABLE(WEB_AUTOFILL)
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    jstring preview = env->NewString(previewSummary.data(), previewSummary.length());
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_setWebTextViewAutoFillable, queryId, preview);
    env->DeleteLocalRef(preview);
#endif
}

bool WebViewCore::drawIsPaused() const
{
    // returning true says scrollview should be offscreen, which pauses
    // gifs. because this is not again queried when we stop scrolling, we don't
    // use the stopping currently.
    return false;
}

void WebViewCore::setWebRequestContextUserAgent()
{
    // We cannot create a WebRequestContext, because we might not know it this is a private tab or not yet
    if (m_webRequestContext)
        m_webRequestContext->setUserAgent(WebFrame::getWebFrame(m_mainFrame)->userAgentForURL(0)); // URL not used
}

void WebViewCore::setWebRequestContextCacheMode(int cacheMode)
{
    m_cacheMode = cacheMode;
    // We cannot create a WebRequestContext, because we might not know it this is a private tab or not yet
    if (!m_webRequestContext)
        return;

    m_webRequestContext->setCacheMode(cacheMode);
}

WebRequestContext* WebViewCore::webRequestContext()
{
    if (!m_webRequestContext) {
        Settings* settings = mainFrame()->settings();
        m_webRequestContext = new WebRequestContext(settings && settings->privateBrowsingEnabled());
        setWebRequestContextUserAgent();
        setWebRequestContextCacheMode(m_cacheMode);
    }
    return m_webRequestContext.get();
}

void WebViewCore::scrollRenderLayer(int layer, const SkRect& rect)
{
#if USE(ACCELERATED_COMPOSITING)
    GraphicsLayerAndroid* root = graphicsRootLayer();
    if (!root)
        return;

    LayerAndroid* layerAndroid = root->platformLayer();
    if (!layerAndroid)
        return;

    LayerAndroid* target = layerAndroid->findById(layer);
    if (!target)
        return;

    RenderLayer* owner = target->owningLayer();
    if (!owner)
        return;

    if (owner->isRootLayer()) {
        FrameView* view = owner->renderer()->frame()->view();
        IntPoint pt(rect.fLeft, rect.fTop);
        view->setScrollPosition(pt);
    } else
        owner->scrollToOffset(rect.fLeft, rect.fTop);
#endif
}

Vector<VisibleSelection> WebViewCore::getTextRanges(
        int startX, int startY, int endX, int endY)
{
    // These are the positions of the selection handles,
    // which reside below the line that they are selecting.
    // Use the vertical position higher, which will include
    // the selected text.
    startY--;
    endY--;
    VisiblePosition startSelect = visiblePositionForContentPoint(startX, startY);
    VisiblePosition endSelect =  visiblePositionForContentPoint(endX, endY);
    Position start = startSelect.deepEquivalent();
    Position end = endSelect.deepEquivalent();
    Vector<VisibleSelection> ranges;
    if (!start.isNull() && !end.isNull()) {
        if (comparePositions(start, end) > 0) {
            swap(start, end); // RTL start/end positions may be swapped
        }
        Position nextRangeStart = start;
        Position previousRangeEnd;
        do {
            VisibleSelection selection(nextRangeStart, end);
            ranges.append(selection);
            previousRangeEnd = selection.end();
            nextRangeStart = nextCandidate(previousRangeEnd);
        } while (comparePositions(previousRangeEnd, end) < 0);
    }
    return ranges;
}

void WebViewCore::deleteText(int startX, int startY, int endX, int endY)
{
    Vector<VisibleSelection> ranges =
            getTextRanges(startX, startY, endX, endY);

    EditorClientAndroid* client = static_cast<EditorClientAndroid*>(
            m_mainFrame->editor()->client());
    client->setUiGeneratedSelectionChange(true);

    SelectionController* selector = m_mainFrame->selection();
    for (size_t i = 0; i < ranges.size(); i++) {
        const VisibleSelection& selection = ranges[i];
        if (selection.isContentEditable()) {
            selector->setSelection(selection, CharacterGranularity);
            Document* document = selection.start().anchorNode()->document();
            WebCore::TypingCommand::deleteSelection(document, 0);
        }
    }
    client->setUiGeneratedSelectionChange(false);
}

void WebViewCore::insertText(const WTF::String &text)
{
    WebCore::Node* focus = currentFocus();
    if (!focus || !isTextInput(focus))
        return;

    Document* document = focus->document();

    EditorClientAndroid* client = static_cast<EditorClientAndroid*>(
            m_mainFrame->editor()->client());
    if (!client)
        return;
    client->setUiGeneratedSelectionChange(true);
    WebCore::TypingCommand::insertText(document, text,
            TypingCommand::PreventSpellChecking);
    client->setUiGeneratedSelectionChange(false);
}

void WebViewCore::resetFindOnPage()
{
    m_searchText.truncate(0);
    m_matchCount = 0;
    m_activeMatchIndex = 0;
    m_activeMatch = 0;
}

int WebViewCore::findTextOnPage(const WTF::String &text)
{
    resetFindOnPage(); // reset even if parameters are bad

    WebCore::Frame* frame = m_mainFrame;
    if (!frame)
        return 0;

    m_searchText = text;
    FindOptions findOptions = WebCore::CaseInsensitive;

    do {
        frame->document()->markers()->removeMarkers(DocumentMarker::TextMatch);
        m_matchCount += frame->editor()->countMatchesForText(text, findOptions,
            0, true);
        updateMatchCount();
        frame->editor()->setMarkedTextMatchesAreHighlighted(true);
        frame = frame->tree()->traverseNextWithWrap(false);
    } while (frame);

    m_activeMatchIndex = m_matchCount - 1; // prime first findNext
    findNextOnPage(true);
    return m_matchCount;
}

void WebViewCore::findNextOnPage(bool forward)
{
    if (!m_mainFrame)
        return;
    if (!m_matchCount)
        return;

    EditorClientAndroid* client = static_cast<EditorClientAndroid*>(
        m_mainFrame->editor()->client());
    client->setUiGeneratedSelectionChange(true);

    // Clear previous active match.
    if (m_activeMatch) {
        m_mainFrame->document()->markers()->setMarkersActive(
            m_activeMatch.get(), false);
    }

    FindOptions findOptions = WebCore::CaseInsensitive
        | WebCore::StartInSelection | WebCore::WrapAround;
    if (!forward)
        findOptions |= WebCore::Backwards;

    // Start from the previous active match.
    if (m_activeMatch) {
        m_mainFrame->selection()->setSelection(m_activeMatch.get());
    }

    bool found = m_mainFrame->editor()->findString(m_searchText, findOptions);
    if (found) {
        VisibleSelection selection(m_mainFrame->selection()->selection());
        if (selection.isNone() || selection.start() == selection.end()) {
            // Temporary workaround for findString() refusing to select text
            // marked "-webkit-user-select: none".
            m_activeMatchIndex = 0;
            m_activeMatch = 0;
        } else {
            // Mark current match "active".
            if (forward) {
                ++m_activeMatchIndex;
                if (m_activeMatchIndex == m_matchCount)
                    m_activeMatchIndex = 0;
            } else {
                if (m_activeMatchIndex == 0)
                    m_activeMatchIndex = m_matchCount;
                --m_activeMatchIndex;
            }
            m_activeMatch = selection.firstRange();
            m_mainFrame->document()->markers()->setMarkersActive(
                m_activeMatch.get(), true);
            m_mainFrame->selection()->revealSelection(
                ScrollAlignment::alignCenterIfNeeded, true);
        }
        updateMatchCount();
    }

    // Clear selection so it doesn't display.
    m_mainFrame->selection()->clear();
    client->setUiGeneratedSelectionChange(false);
}

void WebViewCore::updateMatchCount() const
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject javaObject = m_javaGlue->object(env);
    if (!javaObject.get())
        return;
    jstring javaText = wtfStringToJstring(env, m_searchText, true);
    env->CallVoidMethod(javaObject.get(), m_javaGlue->m_updateMatchCount,
        m_activeMatchIndex, m_matchCount, javaText);
    checkException(env);
}

String WebViewCore::getText(int startX, int startY, int endX, int endY)
{
    String text;

    Vector<VisibleSelection> ranges =
            getTextRanges(startX, startY, endX, endY);

    for (size_t i = 0; i < ranges.size(); i++) {
        const VisibleSelection& selection = ranges[i];
        if (selection.isRange()) {
            PassRefPtr<Range> range = selection.firstRange();
            String textInRange = range->text();
            if (textInRange.length() > 0) {
                if (text.length() > 0)
                    text.append('\n');
                text.append(textInRange);
            }
        }
    }

    return text;
}

//----------------------------------------------------------------------
// Native JNI methods
//----------------------------------------------------------------------
static void RevealSelection(JNIEnv* env, jobject obj, jint nativeClass)
{
    reinterpret_cast<WebViewCore*>(nativeClass)->revealSelection();
}

static jstring RequestLabel(JNIEnv* env, jobject obj, jint nativeClass,
        int framePointer, int nodePointer)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    return wtfStringToJstring(env, viewImpl->requestLabel(
            (WebCore::Frame*) framePointer, (WebCore::Node*) nodePointer));
}

static void ClearContent(JNIEnv* env, jobject obj, jint nativeClass)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    viewImpl->clearContent();
}

static void UpdateFrameCacheIfLoading(JNIEnv* env, jobject obj, jint nativeClass)
{
}

static void SetSize(JNIEnv* env, jobject obj, jint nativeClass, jint width,
        jint height, jint textWrapWidth, jfloat scale, jint screenWidth,
        jint screenHeight, jint anchorX, jint anchorY, jboolean ignoreHeight)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOGV("webviewcore::nativeSetSize(%u %u)\n viewImpl: %p", (unsigned)width, (unsigned)height, viewImpl);
    ALOG_ASSERT(viewImpl, "viewImpl not set in nativeSetSize");
    viewImpl->setSizeScreenWidthAndScale(width, height, textWrapWidth, scale,
            screenWidth, screenHeight, anchorX, anchorY, ignoreHeight);
}

static void SetScrollOffset(JNIEnv* env, jobject obj, jint nativeClass,
        jint gen, jboolean sendScrollEvent, jint x, jint y)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "need viewImpl");

    viewImpl->setScrollOffset(gen, sendScrollEvent, x, y);
}

static void SetGlobalBounds(JNIEnv* env, jobject obj, jint nativeClass,
        jint x, jint y, jint h, jint v)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "need viewImpl");

    viewImpl->setGlobalBounds(x, y, h, v);
}

static jboolean Key(JNIEnv* env, jobject obj, jint nativeClass, jint keyCode,
        jint unichar, jint repeatCount, jboolean isShift, jboolean isAlt,
        jboolean isSym, jboolean isDown)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    return viewImpl->key(PlatformKeyboardEvent(keyCode,
        unichar, repeatCount, isDown, isShift, isAlt, isSym));
}

static void Click(JNIEnv* env, jobject obj, jint nativeClass, int framePtr,
        int nodePtr, jboolean fake)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in Click");

    viewImpl->click(reinterpret_cast<WebCore::Frame*>(framePtr),
        reinterpret_cast<WebCore::Node*>(nodePtr), fake);
}

static void ContentInvalidateAll(JNIEnv* env, jobject obj, jint nativeClass)
{
    reinterpret_cast<WebViewCore*>(nativeClass)->contentInvalidateAll();
}

static void DeleteSelection(JNIEnv* env, jobject obj, jint nativeClass,
        jint start, jint end, jint textGeneration)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    viewImpl->deleteSelection(start, end, textGeneration);
}

static void SetSelection(JNIEnv* env, jobject obj, jint nativeClass,
        jint start, jint end)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    viewImpl->setSelection(start, end);
}

static jstring ModifySelection(JNIEnv* env, jobject obj, jint nativeClass,
        jint direction, jint granularity)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    String selectionString = viewImpl->modifySelection(direction, granularity);
    return wtfStringToJstring(env, selectionString);
}

static void ReplaceTextfieldText(JNIEnv* env, jobject obj, jint nativeClass,
    jint oldStart, jint oldEnd, jstring replace, jint start, jint end,
    jint textGeneration)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    WTF::String webcoreString = jstringToWtfString(env, replace);
    viewImpl->replaceTextfieldText(oldStart,
            oldEnd, webcoreString, start, end, textGeneration);
}

static void PassToJs(JNIEnv* env, jobject obj, jint nativeClass,
    jint generation, jstring currentText, jint keyCode,
    jint keyValue, jboolean down, jboolean cap, jboolean fn, jboolean sym)
{
    WTF::String current = jstringToWtfString(env, currentText);
    reinterpret_cast<WebViewCore*>(nativeClass)->passToJs(generation, current,
        PlatformKeyboardEvent(keyCode, keyValue, 0, down, cap, fn, sym));
}

static void ScrollFocusedTextInput(JNIEnv* env, jobject obj, jint nativeClass,
        jfloat xPercent, jint y)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    viewImpl->scrollFocusedTextInput(xPercent, y);
}

static void SetFocusControllerActive(JNIEnv* env, jobject obj, jint nativeClass,
        jboolean active)
{
    ALOGV("webviewcore::nativeSetFocusControllerActive()\n");
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in nativeSetFocusControllerActive");
    viewImpl->setFocusControllerActive(active);
}

static void SaveDocumentState(JNIEnv* env, jobject obj, jint nativeClass,
        jint frame)
{
    ALOGV("webviewcore::nativeSaveDocumentState()\n");
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in nativeSaveDocumentState");
    viewImpl->saveDocumentState((WebCore::Frame*) frame);
}

void WebViewCore::addVisitedLink(const UChar* string, int length)
{
    if (m_groupForVisitedLinks)
        m_groupForVisitedLinks->addVisitedLink(string, length);
}

static bool UpdateLayers(JNIEnv* env, jobject obj, jint nativeClass,
        jint jbaseLayer)
{
    WebViewCore* viewImpl = (WebViewCore*) nativeClass;
    BaseLayerAndroid* baseLayer = (BaseLayerAndroid*)  jbaseLayer;
    if (baseLayer) {
        LayerAndroid* root = static_cast<LayerAndroid*>(baseLayer->getChild(0));
        if (root)
            return viewImpl->updateLayers(root);
    }
    return true;
}

static void NotifyAnimationStarted(JNIEnv* env, jobject obj, jint nativeClass)
{
    WebViewCore* viewImpl = (WebViewCore*) nativeClass;
    viewImpl->notifyAnimationStarted();
}

static jint RecordContent(JNIEnv* env, jobject obj, jint nativeClass,
        jobject region, jobject pt)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    SkRegion* nativeRegion = GraphicsJNI::getNativeRegion(env, region);
    SkIPoint nativePt;
    BaseLayerAndroid* result = viewImpl->recordContent(nativeRegion, &nativePt);
    GraphicsJNI::ipoint_to_jpoint(nativePt, env, pt);
    return reinterpret_cast<jint>(result);
}

static void SplitContent(JNIEnv* env, jobject obj, jint nativeClass,
        jint content)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    viewImpl->splitContent(reinterpret_cast<PictureSet*>(content));
}

static void SendListBoxChoice(JNIEnv* env, jobject obj, jint nativeClass,
        jint choice)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in nativeSendListBoxChoice");
    viewImpl->popupReply(choice);
}

// Set aside a predetermined amount of space in which to place the listbox
// choices, to avoid unnecessary allocations.
// The size here is arbitrary.  We want the size to be at least as great as the
// number of items in the average multiple-select listbox.
#define PREPARED_LISTBOX_STORAGE 10

static void SendListBoxChoices(JNIEnv* env, jobject obj, jint nativeClass,
        jbooleanArray jArray, jint size)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in nativeSendListBoxChoices");
    jboolean* ptrArray = env->GetBooleanArrayElements(jArray, 0);
    SkAutoSTMalloc<PREPARED_LISTBOX_STORAGE, int> storage(size);
    int* array = storage.get();
    int count = 0;
    for (int i = 0; i < size; i++) {
        if (ptrArray[i]) {
            array[count++] = i;
        }
    }
    env->ReleaseBooleanArrayElements(jArray, ptrArray, JNI_ABORT);
    viewImpl->popupReply(array, count);
}

// TODO: Move this to WebView.cpp since it is only needed there
static jstring FindAddress(JNIEnv* env, jobject obj, jstring addr,
        jboolean caseInsensitive)
{
    if (!addr)
        return 0;
    int length = env->GetStringLength(addr);
    if (!length)
        return 0;
    const jchar* addrChars = env->GetStringChars(addr, 0);
    size_t start, end;
    AddressDetector detector;
    bool success = detector.FindContent(addrChars, addrChars + length, &start, &end);
    jstring ret = 0;
    if (success)
        ret = env->NewString(addrChars + start, end - start);
    env->ReleaseStringChars(addr, addrChars);
    return ret;
}

static jboolean HandleTouchEvent(JNIEnv* env, jobject obj, jint nativeClass,
        jint action, jintArray idArray, jintArray xArray, jintArray yArray,
        jint count, jint actionIndex, jint metaState)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    jint* ptrIdArray = env->GetIntArrayElements(idArray, 0);
    jint* ptrXArray = env->GetIntArrayElements(xArray, 0);
    jint* ptrYArray = env->GetIntArrayElements(yArray, 0);
    Vector<int> ids(count);
    Vector<IntPoint> points(count);
    for (int c = 0; c < count; c++) {
        ids[c] = ptrIdArray[c];
        points[c].setX(ptrXArray[c]);
        points[c].setY(ptrYArray[c]);
    }
    env->ReleaseIntArrayElements(idArray, ptrIdArray, JNI_ABORT);
    env->ReleaseIntArrayElements(xArray, ptrXArray, JNI_ABORT);
    env->ReleaseIntArrayElements(yArray, ptrYArray, JNI_ABORT);

    return viewImpl->handleTouchEvent(action, ids, points, actionIndex, metaState);
}

static void TouchUp(JNIEnv* env, jobject obj, jint nativeClass,
        jint touchGeneration, jint frame, jint node, jint x, jint y)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    viewImpl->touchUp(touchGeneration,
        (WebCore::Frame*) frame, (WebCore::Node*) node, x, y);
}

static jstring RetrieveHref(JNIEnv* env, jobject obj, jint nativeClass,
        jint x, jint y)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    WTF::String result = viewImpl->retrieveHref(x, y);
    if (!result.isEmpty())
        return wtfStringToJstring(env, result);
    return 0;
}

static jstring RetrieveAnchorText(JNIEnv* env, jobject obj, jint nativeClass,
        jint x, jint y)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    WTF::String result = viewImpl->retrieveAnchorText(x, y);
    if (!result.isEmpty())
        return wtfStringToJstring(env, result);
    return 0;
}

static jstring RetrieveImageSource(JNIEnv* env, jobject obj, jint nativeClass,
        jint x, jint y)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    WTF::String result = viewImpl->retrieveImageSource(x, y);
    return !result.isEmpty() ? wtfStringToJstring(env, result) : 0;
}

static void MoveFocus(JNIEnv* env, jobject obj, jint nativeClass, jint framePtr,
        jint nodePtr)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    viewImpl->moveFocus((WebCore::Frame*) framePtr, (WebCore::Node*) nodePtr);
}

static void MoveMouse(JNIEnv* env, jobject obj, jint nativeClass, jint frame,
        jint x, jint y)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    viewImpl->moveMouse((WebCore::Frame*) frame, x, y);
}

static void MoveMouseIfLatest(JNIEnv* env, jobject obj, jint nativeClass,
        jint moveGeneration, jint frame, jint x, jint y)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    viewImpl->moveMouseIfLatest(moveGeneration,
        (WebCore::Frame*) frame, x, y);
}

static void UpdateFrameCache(JNIEnv* env, jobject obj, jint nativeClass)
{
}

static jint GetContentMinPrefWidth(JNIEnv* env, jobject obj, jint nativeClass)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);

    WebCore::Frame* frame = viewImpl->mainFrame();
    if (frame) {
        WebCore::Document* document = frame->document();
        if (document) {
            WebCore::RenderObject* renderer = document->renderer();
            if (renderer && renderer->isRenderView()) {
                return renderer->minPreferredLogicalWidth();
            }
        }
    }
    return 0;
}

static void SetViewportSettingsFromNative(JNIEnv* env, jobject obj,
        jint nativeClass)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);

    WebCore::Settings* s = viewImpl->mainFrame()->page()->settings();
    if (!s)
        return;

#ifdef ANDROID_META_SUPPORT
    env->SetIntField(obj, gWebViewCoreFields.m_viewportWidth, s->viewportWidth());
    env->SetIntField(obj, gWebViewCoreFields.m_viewportHeight, s->viewportHeight());
    env->SetIntField(obj, gWebViewCoreFields.m_viewportInitialScale, s->viewportInitialScale());
    env->SetIntField(obj, gWebViewCoreFields.m_viewportMinimumScale, s->viewportMinimumScale());
    env->SetIntField(obj, gWebViewCoreFields.m_viewportMaximumScale, s->viewportMaximumScale());
    env->SetBooleanField(obj, gWebViewCoreFields.m_viewportUserScalable, s->viewportUserScalable());
    env->SetIntField(obj, gWebViewCoreFields.m_viewportDensityDpi, s->viewportTargetDensityDpi());
#endif
}

static void SetBackgroundColor(JNIEnv* env, jobject obj, jint nativeClass,
        jint color)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);

    viewImpl->setBackgroundColor((SkColor) color);
}

static void DumpDomTree(JNIEnv* env, jobject obj, jint nativeClass,
        jboolean useFile)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);

    viewImpl->dumpDomTree(useFile);
}

static void DumpRenderTree(JNIEnv* env, jobject obj, jint nativeClass,
        jboolean useFile)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);

    viewImpl->dumpRenderTree(useFile);
}

static void DumpNavTree(JNIEnv* env, jobject obj, jint nativeClass)
{
}

static void SetJsFlags(JNIEnv* env, jobject obj, jint nativeClass, jstring flags)
{
    WTF::String flagsString = jstringToWtfString(env, flags);
    WTF::CString utf8String = flagsString.utf8();
    WebCore::ScriptController::setFlags(utf8String.data(), utf8String.length());
}


// Called from the Java side to set a new quota for the origin or new appcache
// max size in response to a notification that the original quota was exceeded or
// that the appcache has reached its maximum size.
static void SetNewStorageLimit(JNIEnv* env, jobject obj, jint nativeClass,
        jlong quota)
{
#if ENABLE(DATABASE) || ENABLE(OFFLINE_WEB_APPLICATIONS)
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    Frame* frame = viewImpl->mainFrame();

    // The main thread is blocked awaiting this response, so now we can wake it
    // up.
    ChromeClientAndroid* chromeC = static_cast<ChromeClientAndroid*>(frame->page()->chrome()->client());
    chromeC->wakeUpMainThreadWithNewQuota(quota);
#endif
}

// Called from Java to provide a Geolocation permission state for the specified origin.
static void GeolocationPermissionsProvide(JNIEnv* env, jobject obj,
        jint nativeClass, jstring origin, jboolean allow, jboolean remember)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    Frame* frame = viewImpl->mainFrame();

    ChromeClientAndroid* chromeClient = static_cast<ChromeClientAndroid*>(frame->page()->chrome()->client());
    chromeClient->provideGeolocationPermissions(jstringToWtfString(env, origin), allow, remember);
}

static void RegisterURLSchemeAsLocal(JNIEnv* env, jobject obj, jint nativeClass,
        jstring scheme)
{
    WebCore::SchemeRegistry::registerURLSchemeAsLocal(jstringToWtfString(env, scheme));
}

static bool FocusBoundsChanged(JNIEnv* env, jobject obj, jint nativeClass)
{
    return reinterpret_cast<WebViewCore*>(nativeClass)->focusBoundsChanged();
}

static void SetIsPaused(JNIEnv* env, jobject obj, jint nativeClass,
        jboolean isPaused)
{
    // tell the webcore thread to stop thinking while we do other work
    // (selection and scrolling). This has nothing to do with the lifecycle
    // pause and resume.
    reinterpret_cast<WebViewCore*>(nativeClass)->setIsPaused(isPaused);
}

static void Pause(JNIEnv* env, jobject obj, jint nativeClass)
{
    // This is called for the foreground tab when the browser is put to the
    // background (and also for any tab when it is put to the background of the
    // browser). The browser can only be killed by the system when it is in the
    // background, so saving the Geolocation permission state now ensures that
    // is maintained when the browser is killed.
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ChromeClient* chromeClient = viewImpl->mainFrame()->page()->chrome()->client();
    ChromeClientAndroid* chromeClientAndroid = static_cast<ChromeClientAndroid*>(chromeClient);
    chromeClientAndroid->storeGeolocationPermissions();

    Frame* mainFrame = viewImpl->mainFrame();
    for (Frame* frame = mainFrame; frame; frame = frame->tree()->traverseNext()) {
        Geolocation* geolocation = frame->domWindow()->navigator()->optionalGeolocation();
        if (geolocation)
            geolocation->suspend();
    }

    viewImpl->deviceMotionAndOrientationManager()->maybeSuspendClients();

    ANPEvent event;
    SkANP::InitEvent(&event, kLifecycle_ANPEventType);
    event.data.lifecycle.action = kPause_ANPLifecycleAction;
    viewImpl->sendPluginEvent(event);

    viewImpl->setIsPaused(true);
}

static void Resume(JNIEnv* env, jobject obj, jint nativeClass)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    Frame* mainFrame = viewImpl->mainFrame();
    for (Frame* frame = mainFrame; frame; frame = frame->tree()->traverseNext()) {
        Geolocation* geolocation = frame->domWindow()->navigator()->optionalGeolocation();
        if (geolocation)
            geolocation->resume();
    }

    viewImpl->deviceMotionAndOrientationManager()->maybeResumeClients();

    ANPEvent event;
    SkANP::InitEvent(&event, kLifecycle_ANPEventType);
    event.data.lifecycle.action = kResume_ANPLifecycleAction;
    viewImpl->sendPluginEvent(event);

    viewImpl->setIsPaused(false);
}

static void FreeMemory(JNIEnv* env, jobject obj, jint nativeClass)
{
    ANPEvent event;
    SkANP::InitEvent(&event, kLifecycle_ANPEventType);
    event.data.lifecycle.action = kFreeMemory_ANPLifecycleAction;
    reinterpret_cast<WebViewCore*>(nativeClass)->sendPluginEvent(event);
}

static void ProvideVisitedHistory(JNIEnv* env, jobject obj, jint nativeClass,
        jobject hist)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    ALOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);

    jobjectArray array = static_cast<jobjectArray>(hist);

    jsize len = env->GetArrayLength(array);
    for (jsize i = 0; i < len; i++) {
        jstring item = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        const UChar* str = static_cast<const UChar*>(env->GetStringChars(item, 0));
        jsize len = env->GetStringLength(item);
        viewImpl->addVisitedLink(str, len);
        env->ReleaseStringChars(item, str);
        env->DeleteLocalRef(item);
    }
}

static void PluginSurfaceReady(JNIEnv* env, jobject obj, jint nativeClass)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    if (viewImpl)
        viewImpl->sendPluginSurfaceReady();
}

// Notification from the UI thread that the plugin's full-screen surface has been discarded
static void FullScreenPluginHidden(JNIEnv* env, jobject obj, jint nativeClass,
        jint npp)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    PluginWidgetAndroid* plugin = viewImpl->getPluginWidget((NPP)npp);
    if (plugin)
        plugin->exitFullScreen(false);
}

static WebCore::IntRect jrect_to_webrect(JNIEnv* env, jobject obj)
{
    int L, T, R, B;
    GraphicsJNI::get_jrect(env, obj, &L, &T, &R, &B);
    return WebCore::IntRect(L, T, R - L, B - T);
}

static bool ValidNodeAndBounds(JNIEnv* env, jobject obj, jint nativeClass,
        int frame, int node, jobject rect)
{
    IntRect nativeRect = jrect_to_webrect(env, rect);
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    return viewImpl->validNodeAndBounds(
            reinterpret_cast<Frame*>(frame),
            reinterpret_cast<Node*>(node), nativeRect);
}

static jobject HitTest(JNIEnv* env, jobject obj, jint nativeClass, jint x,
                       jint y, jint slop, jboolean doMoveMouse)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    if (!viewImpl)
        return 0;
    AndroidHitTestResult result = viewImpl->hitTestAtPoint(x, y, slop, doMoveMouse);
    return result.createJavaObject(env);
}

static void AutoFillForm(JNIEnv* env, jobject obj, jint nativeClass,
        jint queryId)
{
#if ENABLE(WEB_AUTOFILL)
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    if (!viewImpl)
        return;

    WebCore::Frame* frame = viewImpl->mainFrame();
    if (frame) {
        EditorClientAndroid* editorC = static_cast<EditorClientAndroid*>(frame->page()->editorClient());
        WebAutofill* autoFill = editorC->getAutofill();
        autoFill->fillFormFields(queryId);
    }
#endif
}

static void CloseIdleConnections(JNIEnv* env, jobject obj, jint nativeClass)
{
    WebCache::get(true)->closeIdleConnections();
    WebCache::get(false)->closeIdleConnections();
}

static void nativeCertTrustChanged(JNIEnv *env, jobject obj)
{
#if USE(CHROME_NETWORK_STACK)
    WebCache::get(true)->certTrustChanged();
    WebCache::get(false)->certTrustChanged();
#endif
}

static void ScrollRenderLayer(JNIEnv* env, jobject obj, jint nativeClass,
        jint layer, jobject jRect)
{
    SkRect rect;
    GraphicsJNI::jrect_to_rect(env, jRect, &rect);
    reinterpret_cast<WebViewCore*>(nativeClass)->scrollRenderLayer(layer, rect);
}

static void DeleteText(JNIEnv* env, jobject obj, jint nativeClass,
        jint startX, jint startY, jint endX, jint endY)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    viewImpl->deleteText(startX, startY, endX, endY);
}

static void InsertText(JNIEnv* env, jobject obj, jint nativeClass,
        jstring text)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    WTF::String wtfText = jstringToWtfString(env, text);
    viewImpl->insertText(wtfText);
}

static jobject GetText(JNIEnv* env, jobject obj, jint nativeClass,
        jint startX, jint startY, jint endX, jint endY)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    WTF::String text = viewImpl->getText(startX, startY, endX, endY);
    return text.isEmpty() ? 0 : wtfStringToJstring(env, text);
}

static void SelectText(JNIEnv* env, jobject obj, jint nativeClass,
        jint startX, jint startY, jint endX, jint endY)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    viewImpl->selectText(startX, startY, endX, endY);
}

static void ClearSelection(JNIEnv* env, jobject obj, jint nativeClass)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    viewImpl->focusedFrame()->selection()->clear();
}

static void SelectWordAt(JNIEnv* env, jobject obj, jint nativeClass, jint x, jint y)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    viewImpl->selectWordAt(x, y);
}

static void SelectAll(JNIEnv* env, jobject obj, jint nativeClass)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    viewImpl->focusedFrame()->selection()->selectAll();
}

static int FindAll(JNIEnv* env, jobject obj, jint nativeClass,
        jstring text)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    WTF::String wtfText = jstringToWtfString(env, text);
    return viewImpl->findTextOnPage(wtfText);
}

static void FindNext(JNIEnv* env, jobject obj, jint nativeClass,
        jboolean forward)
{
    WebViewCore* viewImpl = reinterpret_cast<WebViewCore*>(nativeClass);
    viewImpl->findNextOnPage(forward);
}

// ----------------------------------------------------------------------------

/*
 * JNI registration.
 */
static JNINativeMethod gJavaWebViewCoreMethods[] = {
    { "nativeClearContent", "(I)V",
            (void*) ClearContent },
    { "nativeFocusBoundsChanged", "(I)Z",
        (void*) FocusBoundsChanged } ,
    { "nativeKey", "(IIIIZZZZ)Z",
        (void*) Key },
    { "nativeClick", "(IIIZ)V",
        (void*) Click },
    { "nativeContentInvalidateAll", "(I)V",
        (void*) ContentInvalidateAll },
    { "nativeSendListBoxChoices", "(I[ZI)V",
        (void*) SendListBoxChoices },
    { "nativeSendListBoxChoice", "(II)V",
        (void*) SendListBoxChoice },
    { "nativeSetSize", "(IIIIFIIIIZ)V",
        (void*) SetSize },
    { "nativeSetScrollOffset", "(IIZII)V",
        (void*) SetScrollOffset },
    { "nativeSetGlobalBounds", "(IIIII)V",
        (void*) SetGlobalBounds },
    { "nativeSetSelection", "(III)V",
        (void*) SetSelection } ,
    { "nativeModifySelection", "(III)Ljava/lang/String;",
        (void*) ModifySelection },
    { "nativeDeleteSelection", "(IIII)V",
        (void*) DeleteSelection } ,
    { "nativeReplaceTextfieldText", "(IIILjava/lang/String;III)V",
        (void*) ReplaceTextfieldText } ,
    { "nativeMoveFocus", "(III)V",
        (void*) MoveFocus },
    { "nativeMoveMouse", "(IIII)V",
        (void*) MoveMouse },
    { "nativeMoveMouseIfLatest", "(IIIII)V",
        (void*) MoveMouseIfLatest },
    { "passToJs", "(IILjava/lang/String;IIZZZZ)V",
        (void*) PassToJs },
    { "nativeScrollFocusedTextInput", "(IFI)V",
        (void*) ScrollFocusedTextInput },
    { "nativeSetFocusControllerActive", "(IZ)V",
        (void*) SetFocusControllerActive },
    { "nativeSaveDocumentState", "(II)V",
        (void*) SaveDocumentState },
    { "nativeFindAddress", "(Ljava/lang/String;Z)Ljava/lang/String;",
        (void*) FindAddress },
    { "nativeHandleTouchEvent", "(II[I[I[IIII)Z",
            (void*) HandleTouchEvent },
    { "nativeTouchUp", "(IIIIII)V",
        (void*) TouchUp },
    { "nativeRetrieveHref", "(III)Ljava/lang/String;",
        (void*) RetrieveHref },
    { "nativeRetrieveAnchorText", "(III)Ljava/lang/String;",
        (void*) RetrieveAnchorText },
    { "nativeRetrieveImageSource", "(III)Ljava/lang/String;",
        (void*) RetrieveImageSource },
    { "nativeUpdateFrameCache", "(I)V",
        (void*) UpdateFrameCache },
    { "nativeGetContentMinPrefWidth", "(I)I",
        (void*) GetContentMinPrefWidth },
    { "nativeUpdateLayers", "(II)Z",
        (void*) UpdateLayers },
    { "nativeNotifyAnimationStarted", "(I)V",
        (void*) NotifyAnimationStarted },
    { "nativeRecordContent", "(ILandroid/graphics/Region;Landroid/graphics/Point;)I",
        (void*) RecordContent },
    { "setViewportSettingsFromNative", "(I)V",
        (void*) SetViewportSettingsFromNative },
    { "nativeSplitContent", "(II)V",
        (void*) SplitContent },
    { "nativeSetBackgroundColor", "(II)V",
        (void*) SetBackgroundColor },
    { "nativeRegisterURLSchemeAsLocal", "(ILjava/lang/String;)V",
        (void*) RegisterURLSchemeAsLocal },
    { "nativeDumpDomTree", "(IZ)V",
        (void*) DumpDomTree },
    { "nativeDumpRenderTree", "(IZ)V",
        (void*) DumpRenderTree },
    { "nativeDumpNavTree", "(I)V",
        (void*) DumpNavTree },
    { "nativeSetNewStorageLimit", "(IJ)V",
        (void*) SetNewStorageLimit },
    { "nativeGeolocationPermissionsProvide", "(ILjava/lang/String;ZZ)V",
        (void*) GeolocationPermissionsProvide },
    { "nativeSetIsPaused", "(IZ)V", (void*) SetIsPaused },
    { "nativePause", "(I)V", (void*) Pause },
    { "nativeResume", "(I)V", (void*) Resume },
    { "nativeFreeMemory", "(I)V", (void*) FreeMemory },
    { "nativeSetJsFlags", "(ILjava/lang/String;)V", (void*) SetJsFlags },
    { "nativeRequestLabel", "(III)Ljava/lang/String;",
        (void*) RequestLabel },
    { "nativeRevealSelection", "(I)V", (void*) RevealSelection },
    { "nativeUpdateFrameCacheIfLoading", "(I)V",
        (void*) UpdateFrameCacheIfLoading },
    { "nativeProvideVisitedHistory", "(I[Ljava/lang/String;)V",
        (void*) ProvideVisitedHistory },
    { "nativeFullScreenPluginHidden", "(II)V",
        (void*) FullScreenPluginHidden },
    { "nativePluginSurfaceReady", "(I)V",
        (void*) PluginSurfaceReady },
    { "nativeValidNodeAndBounds", "(IIILandroid/graphics/Rect;)Z",
        (void*) ValidNodeAndBounds },
    { "nativeHitTest", "(IIIIZ)Landroid/webkit/WebViewCore$WebKitHitTest;",
        (void*) HitTest },
    { "nativeAutoFillForm", "(II)V",
        (void*) AutoFillForm },
    { "nativeScrollLayer", "(IILandroid/graphics/Rect;)V",
        (void*) ScrollRenderLayer },
    { "nativeCloseIdleConnections", "(I)V",
        (void*) CloseIdleConnections },
    { "nativeDeleteText", "(IIIII)V",
        (void*) DeleteText },
    { "nativeInsertText", "(ILjava/lang/String;)V",
        (void*) InsertText },
    { "nativeGetText", "(IIIII)Ljava/lang/String;",
        (void*) GetText },
    { "nativeSelectText", "(IIIII)V",
        (void*) SelectText },
    { "nativeClearTextSelection", "(I)V",
        (void*) ClearSelection },
    { "nativeSelectWordAt", "(III)V",
        (void*) SelectWordAt },
    { "nativeSelectAll", "(I)V",
        (void*) SelectAll },
    { "nativeCertTrustChanged","()V",
        (void*) nativeCertTrustChanged },
    { "nativeFindAll", "(ILjava/lang/String;)I",
        (void*) FindAll },
    { "nativeFindNext", "(IZ)V",
        (void*) FindNext },
};

int registerWebViewCore(JNIEnv* env)
{
    jclass widget = env->FindClass("android/webkit/WebViewCore");
    ALOG_ASSERT(widget,
            "Unable to find class android/webkit/WebViewCore");
    gWebViewCoreFields.m_nativeClass = env->GetFieldID(widget, "mNativeClass",
            "I");
    ALOG_ASSERT(gWebViewCoreFields.m_nativeClass,
            "Unable to find android/webkit/WebViewCore.mNativeClass");
    gWebViewCoreFields.m_viewportWidth = env->GetFieldID(widget,
            "mViewportWidth", "I");
    ALOG_ASSERT(gWebViewCoreFields.m_viewportWidth,
            "Unable to find android/webkit/WebViewCore.mViewportWidth");
    gWebViewCoreFields.m_viewportHeight = env->GetFieldID(widget,
            "mViewportHeight", "I");
    ALOG_ASSERT(gWebViewCoreFields.m_viewportHeight,
            "Unable to find android/webkit/WebViewCore.mViewportHeight");
    gWebViewCoreFields.m_viewportInitialScale = env->GetFieldID(widget,
            "mViewportInitialScale", "I");
    ALOG_ASSERT(gWebViewCoreFields.m_viewportInitialScale,
            "Unable to find android/webkit/WebViewCore.mViewportInitialScale");
    gWebViewCoreFields.m_viewportMinimumScale = env->GetFieldID(widget,
            "mViewportMinimumScale", "I");
    ALOG_ASSERT(gWebViewCoreFields.m_viewportMinimumScale,
            "Unable to find android/webkit/WebViewCore.mViewportMinimumScale");
    gWebViewCoreFields.m_viewportMaximumScale = env->GetFieldID(widget,
            "mViewportMaximumScale", "I");
    ALOG_ASSERT(gWebViewCoreFields.m_viewportMaximumScale,
            "Unable to find android/webkit/WebViewCore.mViewportMaximumScale");
    gWebViewCoreFields.m_viewportUserScalable = env->GetFieldID(widget,
            "mViewportUserScalable", "Z");
    ALOG_ASSERT(gWebViewCoreFields.m_viewportUserScalable,
            "Unable to find android/webkit/WebViewCore.mViewportUserScalable");
    gWebViewCoreFields.m_viewportDensityDpi = env->GetFieldID(widget,
            "mViewportDensityDpi", "I");
    ALOG_ASSERT(gWebViewCoreFields.m_viewportDensityDpi,
            "Unable to find android/webkit/WebViewCore.mViewportDensityDpi");
    gWebViewCoreFields.m_webView = env->GetFieldID(widget,
            "mWebView", "Landroid/webkit/WebViewClassic;");
    ALOG_ASSERT(gWebViewCoreFields.m_webView,
            "Unable to find android/webkit/WebViewCore.mWebView");
    gWebViewCoreFields.m_drawIsPaused = env->GetFieldID(widget,
            "mDrawIsPaused", "Z");
    ALOG_ASSERT(gWebViewCoreFields.m_drawIsPaused,
            "Unable to find android/webkit/WebViewCore.mDrawIsPaused");
    gWebViewCoreFields.m_lowMemoryUsageMb = env->GetFieldID(widget, "mLowMemoryUsageThresholdMb", "I");
    gWebViewCoreFields.m_highMemoryUsageMb = env->GetFieldID(widget, "mHighMemoryUsageThresholdMb", "I");
    gWebViewCoreFields.m_highUsageDeltaMb = env->GetFieldID(widget, "mHighUsageDeltaMb", "I");

    gWebViewCoreStaticMethods.m_isSupportedMediaMimeType =
        env->GetStaticMethodID(widget, "isSupportedMediaMimeType", "(Ljava/lang/String;)Z");
    LOG_FATAL_IF(!gWebViewCoreStaticMethods.m_isSupportedMediaMimeType,
        "Could not find static method isSupportedMediaMimeType from WebViewCore");

    env->DeleteLocalRef(widget);

    return jniRegisterNativeMethods(env, "android/webkit/WebViewCore",
            gJavaWebViewCoreMethods, NELEM(gJavaWebViewCoreMethods));
}

} /* namespace android */
