//
// Notice Regarding Standards.  AMD does not provide a license or sublicense to
// any Intellectual Property Rights relating to any standards, including but not
// limited to any audio and/or video codec technologies such as MPEG-2, MPEG-4;
// AVC/H.264; HEVC/H.265; AAC decode/FFMPEG; AAC encode/FFMPEG; VC-1; and MP3
// (collectively, the "Media Technologies"). For clarity, you will pay any
// royalties due for such third party technologies, which may include the Media
// Technologies that are owed as a result of AMD providing the Software to you.
//
// MIT license
//
//
// Copyright (c) 2024 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// DVR.cpp : Defines the entry point for the application.
//

#include "public/common/AMFFactory.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderAV1.h"
#include "public/include/core/Debug.h"

#include "public/samples/CPPSamples/common/CmdLineParser.h"
#include "public/samples/CPPSamples/common/CmdLogger.h"
#include "public/samples/CPPSamples/common/ParametersStorage.h"
#include "public/samples/CPPSamples/common/DisplayDvrPipeline.h"
#include "public/samples/CPPSamples/common/DeviceVulkan.h"
#include "public/samples/CPPSamples/common/CmdLineParser.h"
#include "public/samples/CPPSamples/common/CAmfInit.h"
#include "public/common/Linux/DRMDevice.h"

#include <dialog.h>
#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <iostream>

struct ProgramState {
    GtkBuilder* pBuilder;
    DisplayDvrPipeline* pPipeline;
private:
    GtkTextBuffer* m_pMessageBuffer;
    GtkScrolledWindow* m_pScrollWindow;
    std::set<int> m_monitorIdSet;
public:
    ProgramState(GtkBuilder* b, DisplayDvrPipeline* p)
        : pBuilder(b)
        , pPipeline(p)
    {
        GtkTextView* pTextView = GTK_TEXT_VIEW(GetWidget("messages"));
        m_pMessageBuffer = gtk_text_view_get_buffer(pTextView);
        m_pScrollWindow = GTK_SCROLLED_WINDOW(GetWidget("messages_scroll"));
    }

    GtkWidget* GetWidget(const gchar* pName) {
        return GTK_WIDGET(gtk_builder_get_object(pBuilder, pName));
    }

    static int ScrollToBottom(ProgramState* state) {
        GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(state->m_pScrollWindow);
        gtk_adjustment_set_value(adjustment, gtk_adjustment_get_upper(adjustment));
        return G_SOURCE_REMOVE;
    }

    void LogMessage(const std::string& message) {
        GtkTextIter iter;
        gtk_text_buffer_get_end_iter(m_pMessageBuffer, &iter);
        gtk_text_buffer_insert(m_pMessageBuffer, &iter, message.c_str(), message.size());
        gtk_text_buffer_insert(m_pMessageBuffer, &iter, "\n", 1);
        //scroll to bottom after a timeout so that GTK has
        //time to figure out how long the scroll container is
        g_timeout_add(10, G_SOURCE_FUNC(ProgramState::ScrollToBottom), this);
    }

    void UpdateMonitorIDs(bool printLog) {
        std::string monitorIDsString;
        bool first = true;
        for (const int& id : m_monitorIdSet)
        {
            if (first != true)
            {
                monitorIDsString += ",";
            }
            monitorIDsString += std::to_string(id);
            first = false;
        }
        amf_wstring monitorIDsWString = amf::amf_from_utf8_to_unicode(monitorIDsString.c_str());
        pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_MONITORID, monitorIDsWString.c_str());
        if (printLog == true)
        {
            if (monitorIDsString == "")
            {
                LogMessage("Not recording a monitor! Please select one from the sources dropdown.");
            }
            else
            {
                LogMessage("Recording from monitors: "+monitorIDsString);
            }
        }
    }

    void ToggleMonitorID(int monitorId, bool toggle) {
        if (toggle == true)
        {
            m_monitorIdSet.insert(monitorId);
        }
        else
        {
            m_monitorIdSet.erase(monitorId);
        }
        UpdateMonitorIDs(true);
    }

    void ResetMonitorIDs() {
        m_monitorIdSet.clear();
        UpdateMonitorIDs(false);
    }
};

int OnFPSUpdate(ProgramState* pState) {
    GtkWidget* pFPSCounter = pState->GetWidget("fps_counter");

    double fps = pState->pPipeline->GetFPS();
    amf_string fpsText = amf::amf_string_format("FPS: %.1f", fps);
    gtk_label_set_text(GTK_LABEL(pFPSCounter), fpsText.c_str());

    return TRUE; //keep the timer going
}

guint gFPSTimer = 0;
bool gFPSTimerExists = false;

void UpdateIsRecording(ProgramState* pState, bool isRecording) {
    GtkWidget *pRec = pState->GetWidget("record");
    GtkWidget *pStop = pState->GetWidget("stop");

    gtk_widget_set_sensitive(pRec, !isRecording);
    gtk_widget_set_sensitive(pStop, isRecording);

    if (gFPSTimerExists == true)
    {
        g_source_remove(gFPSTimer);
        gFPSTimerExists = false;
    }

    if (isRecording)
    {
        //FPS timer every 1 second
        gFPSTimer = g_timeout_add(1000, G_SOURCE_FUNC(OnFPSUpdate), pState);
        gFPSTimerExists = true;
    }
    else
    {
        //clear FPS label
        GtkWidget* pFPSCounter = pState->GetWidget("fps_counter");
        gtk_label_set_text(GTK_LABEL(pFPSCounter), "");
    }
}

void OnRecordClicked(GtkButton*, ProgramState* pState)
{
    AMF_RESULT res = pState->pPipeline->Init();
    if (res != AMF_OK)
    {
        pState->LogMessage("Failed to initialize pipeline.");
        pState->pPipeline->Terminate();
        return;
    }
    res = pState->pPipeline->Start();
    if (res != AMF_OK)
    {
        pState->LogMessage("Failed to start pipeline.");
        pState->pPipeline->Terminate();
        return;
    }
    pState->LogMessage("Recording started.");

    UpdateIsRecording(pState, true);
}

void OnStopClicked(GtkButton*, ProgramState* pState)
{
    UpdateIsRecording(pState, false);

    pState->pPipeline->Stop();
    pState->LogMessage("Recording stopped.");
}

void ConnectRecAndStop(ProgramState* pState) {
    GtkWidget *rec = pState->GetWidget("record");
    g_signal_connect(rec, "clicked", G_CALLBACK(OnRecordClicked), pState);
    GtkWidget *stop = pState->GetWidget("stop");
    g_signal_connect(stop, "clicked", G_CALLBACK(OnStopClicked), pState);
    gtk_widget_set_sensitive(stop, false);
}

gboolean OnAboutDialogClosed(GtkWidget* pWidget, GdkEvent*, gpointer) {
    gtk_widget_hide(pWidget);
    return true;
}

void OnAboutClicked(GtkMenuItem* item, ProgramState* pState) {
    GtkWidget* pAboutDialog = pState->GetWidget("about_dialog");
    g_signal_connect(pAboutDialog, "delete-event", G_CALLBACK(OnAboutDialogClosed), NULL);
    gtk_widget_show_all(pAboutDialog);
}

void ConnectHelpMenu(ProgramState* pState) {
    GtkMenuItem *pAboutMenuItem = GTK_MENU_ITEM(pState->GetWidget("about_button"));
    g_signal_connect(pAboutMenuItem, "activate", G_CALLBACK(OnAboutClicked), pState);
}

void OnSaveClicked(GtkMenuItem*, ProgramState* pState) {
    GtkWindow* pWindow = GTK_WINDOW(pState->GetWidget("main_window"));
    GtkWidget* pDialog = gtk_file_chooser_dialog_new("Choose Location", pWindow,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Open"), GTK_RESPONSE_ACCEPT,
        NULL);

    amf_wstring outputPath = L"";
    pState->pPipeline->GetParamWString(DisplayDvrPipeline::PARAM_NAME_OUTPUT, outputPath);
    amf_string default_filename = amf::amf_from_unicode_to_utf8(outputPath);
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(pDialog), default_filename.c_str());
    // for some reason, gtk doesn't fill the name of a non-existent file, so we have to do it manually
    char* pBase = strdup(default_filename.c_str());
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(pDialog), basename(pBase));
    free(pBase);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(pDialog), TRUE);

    gint res = gtk_dialog_run(GTK_DIALOG(pDialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *pChooser = GTK_FILE_CHOOSER(pDialog);
        gchar* pFilepath = gtk_file_chooser_get_filename(pChooser);

        amf_wstring newPath = amf::amf_from_utf8_to_unicode(pFilepath);
        pState->pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_OUTPUT, newPath.c_str());

        pState->LogMessage("Saving to "+std::string(pFilepath));
        g_free(pFilepath);
    }
    gtk_widget_destroy(pDialog);
}

void ConnectFileMenu(ProgramState* pState) {
    GtkMenuItem *pSaveMenuItem = GTK_MENU_ITEM(pState->GetWidget("change_save_location"));
    g_signal_connect(pSaveMenuItem, "activate", G_CALLBACK(OnSaveClicked), pState);
}

void SetDefaultFilename(ProgramState* pState) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));

    amf_string filepath = amf::amf_string_format("%s/DVRRecording-%d-%02d-%02d-%02d-%02d-%02d.mp4",
        cwd,
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec);
    amf_string logMessage = "Saving to " + filepath;
    pState->LogMessage(logMessage.c_str());

    amf_wstring wfilepath = amf::amf_from_utf8_to_unicode(filepath);
    pState->pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_OUTPUT, wfilepath.c_str());
}

AMF_RESULT GetAdapterList(std::vector<VulkanPhysicalDeviceInfo>& adapters) {
    static bool adapterListInitialized = false;
    static std::vector<VulkanPhysicalDeviceInfo> adapterList;

    AMF_RESULT res = AMF_OK;
    if (adapterListInitialized == false)
    {
        amf::AMFContextPtr pContext = nullptr;
        res = g_AMFFactory.GetFactory()->CreateContext(&pContext);
        CHECK_AMF_ERROR_RETURN(res, "Couldn't create context!");

        res = DeviceVulkan::GetAdapterList(pContext, adapterList);
        CHECK_AMF_ERROR_RETURN(res, "Couldn't get adapters list!");

        adapterListInitialized = true;
    }
    adapters = adapterList;

    return AMF_OK;
}

void OnDisplayToggled(GtkCheckMenuItem* pCheckMenuItem, ProgramState* pState) {
    bool isChecked = gtk_check_menu_item_get_active(pCheckMenuItem);

    int* pIndex = (int*)g_object_get_data(G_OBJECT(pCheckMenuItem), "display_id");
    pState->ToggleMonitorID(*pIndex, isChecked);
    // pState->pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_ADAPTERID, *pIndex);
}

void SetupCaptureSourceList(ProgramState* pState, int adapterId) {
    GtkWidget* pMenu = gtk_menu_new();
    GtkWidget* pSourcesMenu = pState->GetWidget("sources_menu");
    // if there was already a submenu, the previous and all of its children get destroyed
    // so we don't have to destroy them ourselves. three cheers for reference counting!
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(pSourcesMenu), pMenu);

    pState->ResetMonitorIDs();

    std::vector<VulkanPhysicalDeviceInfo> adapters;
    GetAdapterList(adapters);
    if (adapterId >= adapters.size()) {
        LOG_ERROR(L"adapterId out of range");
        return;
    }
    const VulkanPhysicalDeviceInfo& adapter = adapters[adapterId];
    DRMDevice drm; //this will be automatically Terminate'd when it's destroyed
    const VkPhysicalDevicePCIBusInfoPropertiesEXT& pciBusInfo = adapter.pciBusInfo;
    if (drm.InitFromVulkan(pciBusInfo.pciDomain, pciBusInfo.pciBus, pciBusInfo.pciDevice, pciBusInfo.pciFunction) != AMF_OK)
    {
        LOG_ERROR(L"couldn't init DRM from vulkan card!");
        return;
    }

    std::vector<DRMCRTC> crtcs;
    if (drm.GetCRTCs(crtcs) != AMF_OK)
    {
        return;
    }

    for (size_t i = 0; i < crtcs.size(); i++)
    {
        const DRMCRTC& crtc = crtcs[i];
        amf_string crtcName = amf::amf_string_format("CRTC=%d (%dx%d+%d+%d)",
                crtc.crtcID,
                crtc.crop.Width(),
                crtc.crop.Height(),
                crtc.crop.left,
                crtc.crop.top);
        GtkWidget* pItem = gtk_check_menu_item_new_with_label(crtcName.c_str());
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(pItem), i == 0);

        // assign the index as data on the menu item, so it can be accessed in the signal handler
        int* pIndex = (int*)malloc(sizeof(int));
        *pIndex = i;
        g_object_set_data_full(G_OBJECT(pItem), "display_id", pIndex, free);
        g_signal_connect(pItem, "toggled", G_CALLBACK(OnDisplayToggled), pState);

        gtk_menu_shell_append(GTK_MENU_SHELL(pMenu), pItem);
        gtk_widget_show(pItem);

        if (i == 0)
        {
            pState->ToggleMonitorID(i, true);
        }
    }
}

void OnAdapterToggled(GtkCheckMenuItem* pCheckMenuItem, ProgramState* pState) {
    if (gtk_check_menu_item_get_active(pCheckMenuItem) == false) {
        return;
    }
    int* pIndex = (int*)g_object_get_data(G_OBJECT(pCheckMenuItem), "adapter_id");
    pState->LogMessage("Choosing adapter "+std::to_string(*pIndex));
    pState->pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_ADAPTERID, *pIndex);

    SetupCaptureSourceList(pState, *pIndex);
}

void SetupDevicesList(ProgramState* pState) {
    GtkWidget* pMenu = gtk_menu_new();
    GtkWidget* pDevicesMenu = pState->GetWidget("devices_menu");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(pDevicesMenu), pMenu);

    std::vector<VulkanPhysicalDeviceInfo> adapters;
    GetAdapterList(adapters);

    GSList *pGroup = NULL;

    for (size_t i = 0; i < adapters.size(); i++) {
        const VulkanPhysicalDeviceInfo& adapter = adapters[i];
        GtkWidget* pItem = gtk_radio_menu_item_new_with_label(pGroup, adapter.props.properties.deviceName);

        pGroup = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(pItem));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(pItem), i == 0);

        // assign the index as data on the menu item, so it can be accessed in the signal handler
        int* pIndex = (int*)malloc(sizeof(int));
        *pIndex = i;
        g_object_set_data_full(G_OBJECT(pItem), "adapter_id", pIndex, free);
        g_signal_connect(pItem, "toggled", G_CALLBACK(OnAdapterToggled), pState);

        gtk_menu_shell_append(GTK_MENU_SHELL(pMenu), pItem);
        gtk_widget_show(pItem);

        if (i == 0)
        {
            SetupCaptureSourceList(pState, i);
        }
    }
}

void SetupCaptureComponentList(ProgramState* pState) {
    GtkWidget* pMenu = gtk_menu_new();
    GtkWidget* pComponentMenu = pState->GetWidget("component_menu");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(pComponentMenu), pMenu);

    GSList *pGroup = NULL;
    GtkWidget* pItem = gtk_radio_menu_item_new_with_label(pGroup, "AMD DRM Capture");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(pItem), true);
    gtk_menu_shell_append(GTK_MENU_SHELL(pMenu), pItem);
    gtk_widget_show(pItem);

    // todo: add selection logic if we create more capture components
}

int main(int argc, char** argv) {
    CAmfInit  amfInit;
    AMF_RESULT res = amfInit.Init();
    if (res != AMF_OK)
    {
        LOG_ERROR(L"AMF failed to initialize");
        return 1;
    }
    g_AMFFactory.GetDebug()->AssertsEnable(false);

    gtk_init(&argc, &argv);
    GtkBuilder *pBuilder = gtk_builder_new_from_resource("/com/amd/amf/samples/dvr/DVR.glade");

    DisplayDvrPipeline pipeline;
    ProgramState state(pBuilder, &pipeline);

    std::wstring codec = L"AMFVideoEncoderVCE_AVC";
    pipeline.SetParam(DisplayDvrPipeline::PARAM_NAME_CODEC, AMFVideoEncoderVCE_AVC);
    RegisterEncoderParamsAVC(&pipeline);

    if (!parseCmdLineParameters(&pipeline, argc, argv)) {
        return -1;
    }

    ConnectRecAndStop(&state);
    ConnectFileMenu(&state);
    ConnectHelpMenu(&state);
    SetupDevicesList(&state);
    SetupCaptureComponentList(&state);
    gtk_builder_connect_signals(pBuilder, NULL);
    GtkWidget* pWindow = state.GetWidget("main_window");

    SetDefaultFilename(&state);

    gtk_widget_show_all(pWindow);
    gtk_main();

    return 0;
}