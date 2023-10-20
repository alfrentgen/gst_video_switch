#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <gst/gst.h>

static GMainLoop* loop = NULL;

static const gchar* rtsp_address = "rtsp://admin:Admin12345@reg.fuzzun.ru:50232/ISAPI/Streaming/Channels/101";

static gboolean my_bus_callback(GstBus* bus, GstMessage* message, gpointer data)
{
    g_print("Got %s message\n", GST_MESSAGE_TYPE_NAME(message));

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError* err;
            gchar* debug;

            gst_message_parse_error(message, &err, &debug);
            g_print("Error: %s\n", err->message);
            g_error_free(err);
            g_free(debug);

            g_main_loop_quit(loop);
            break;
        }
        case GST_MESSAGE_EOS:
            /* end-of-stream */
            g_main_loop_quit(loop);
            break;
        default:
            /* unhandled message */
            break;
    }

    /* we want to be notified again the next time there is a message
     * on the bus, so returning TRUE (FALSE means we want to stop watching
     * for messages on the bus and our callback should not be called again)
     */
    return TRUE;
}

static gboolean switch_timer(GstElement* video_switch)
{
    gint nb_sources;
    GstPad *active_pad, *next_active_pad;
    gchar* active_name;

    g_message("switching");
    g_object_get(G_OBJECT(video_switch), "n-pads", &nb_sources, NULL);
    g_object_get(G_OBJECT(video_switch), "active-pad", &active_pad, NULL);
    if (active_pad) {
        active_name = gst_pad_get_name(active_pad);
        auto* next_active_pad_name = strcmp(active_name, "sink_0") == 0 ? "sink_1" : "sink_0";
        next_active_pad = gst_element_get_static_pad(video_switch, next_active_pad_name);
        if (next_active_pad) {
            g_object_set(G_OBJECT(video_switch), "active-pad", next_active_pad, NULL);
            gst_object_unref(next_active_pad);
        }

        g_free(active_name);

        g_message("current number of sources : %d, active source %s", nb_sources, gst_pad_get_name(active_pad));
    }

    return (GST_STATE(GST_ELEMENT(video_switch)) == GST_STATE_PLAYING);
}

static void last_message_received(GObject* segment)
{
    gchar* last_message;

    g_object_get(segment, "last_message", &last_message, NULL);
    g_print("last-message: %s\n", last_message);
    g_free(last_message);
}

static void pad_added_cb(GstElement* element, GstPad* pad, gpointer data)
{
    gchar* name;
    GstCaps* p_caps;
    gchar* description;

    name = gst_pad_get_name(pad);
    g_print("A new pad %s was created\n", name);

    p_caps = gst_pad_get_pad_template_caps(pad);

    description = gst_caps_to_string(p_caps);
    // printf("%s\n", p_caps, ", ", description, "\n");
    g_free(description);

    auto* downstream = GST_ELEMENT(data);

    // try to link the pads then ...
    if (!gst_element_link_pads(element, name, downstream, "sink")) {
        g_message("Failed to link elements 3\n");
    }

    g_free(name);
}

int main(int argc, char* argv[])
{
    /* Check input arguments */
    std::optional<std::string> video_file_path;
    if (argv[1]) {
        if (std::string arg1(argv[1]); arg1 == "help") {
            g_printerr("Usage: %s [Video file name] [Switching period]\n", argv[0]);
            return 0;
        }
        else {
            video_file_path = argv[1];
        }
    }

    /* Set up the pipeline */
    guint switch_period = 5000;
    if (argc == 3) {
        try {
            switch_period = std::stoul(argv[2]);
        }
        catch (...) {
            std::cout << "Incorrect switching period was specified: " << argv[2] << ". Applying default: " << switch_period << std::endl;
        }
    }

    /* Initing GStreamer library */
    gst_init(&argc, &argv);

    loop = g_main_loop_new(NULL, FALSE);

    auto* pipeline = gst_pipeline_new("pipeline");

    auto make_element_and_add_to_bin = [&](const gchar* element_name, const gchar* alias) {
        auto* element = gst_element_factory_make(element_name, alias);
        assert(gst_bin_add(GST_BIN(pipeline), element));
        return element;
    };
    // first input pipe
    "rtspsrc location=rtsp://admin:Admin12345@reg.fuzzun.ru:50232/ISAPI/Streaming/Channels/101 latency=100 !\
     queue ! rtph265depay ! h265parse ! avdec_h265";
    std::vector<GstElement*> input_segment1;
    auto* element = make_element_and_add_to_bin("rtspsrc", "src0");
    g_object_set(G_OBJECT(element), "location", rtsp_address, NULL);
    // g_object_set(G_OBJECT(element), "latency", 1000, NULL);
    // input_segment1.emplace_back(gst_element_factory_make("queue", "queue0"));
    input_segment1.emplace_back(make_element_and_add_to_bin("rtph265depay", "depay0"));
    assert(g_signal_connect(element, "pad-added", G_CALLBACK(pad_added_cb), input_segment1.front()));
    input_segment1.emplace_back(make_element_and_add_to_bin("h265parse", "parse0"));
    input_segment1.emplace_back(make_element_and_add_to_bin("avdec_h265", "decode0"));
    input_segment1.emplace_back(make_element_and_add_to_bin("videoconvert", "conv0"));
    input_segment1.emplace_back(make_element_and_add_to_bin("videoscale", "scale0"));
    input_segment1.emplace_back(make_element_and_add_to_bin("capsfilter", "caps0"));
    g_object_set(G_OBJECT(input_segment1.back()), "caps", gst_caps_from_string("video/x-raw,width=640,height=480"), NULL);
    input_segment1.emplace_back(make_element_and_add_to_bin("identity", "sink0_sync"));
    g_object_set(G_OBJECT(input_segment1.back()), "sync", TRUE, NULL);

    // second input pipe
    std::vector<GstElement*> input_segment2;
    if (video_file_path; std::ifstream(std::filesystem::path(*video_file_path))) {
        auto* decbin = make_element_and_add_to_bin("uridecodebin", "dec1");
        const std::string uri = "file://" + *video_file_path;
        g_object_set(G_OBJECT(decbin), "uri", uri.c_str(), NULL);
        input_segment2.emplace_back(make_element_and_add_to_bin("videoconvert", "conv1"));
        input_segment2.emplace_back(make_element_and_add_to_bin("videoscale", "scale1"));
        input_segment2.emplace_back(make_element_and_add_to_bin("capsfilter", "caps1"));
        g_object_set(G_OBJECT(input_segment2.back()), "caps", gst_caps_from_string("video/x-raw,width=640,height=480"), NULL);
        assert(g_signal_connect(decbin, "pad-added", G_CALLBACK(pad_added_cb), input_segment2.front()));
    }
    else {
        std::cout << "Could not open input file, using 'videotestsrc'" << std::endl;
        input_segment2.emplace_back(make_element_and_add_to_bin("videotestsrc", "src1"));
        g_object_set(G_OBJECT(input_segment2.back()), "pattern", 1, NULL);
        input_segment2.emplace_back(make_element_and_add_to_bin("capsfilter", "caps1"));
        g_object_set(G_OBJECT(input_segment2.back()), "caps", gst_caps_from_string("video/x-raw,width=640,height=480"), NULL);
    }
    input_segment2.emplace_back(make_element_and_add_to_bin("identity", "sink1_sync"));
    g_object_set(G_OBJECT(input_segment2.back()), "sync", TRUE, NULL);

    // output pipe
    std::vector<GstElement*> output_pipe;
    output_pipe.emplace_back(make_element_and_add_to_bin("input-selector", "video_switch"));
    auto* video_switch = output_pipe.back();
    output_pipe.emplace_back(make_element_and_add_to_bin("identity", "identity-segment"));
    element = output_pipe.back();
    g_object_set(G_OBJECT(element), "silent", TRUE, NULL);
    g_signal_connect(G_OBJECT(element), "notify::last-message", G_CALLBACK(last_message_received), element);
    g_object_set(G_OBJECT(element), "single-segment", TRUE, NULL);

    output_pipe.emplace_back(make_element_and_add_to_bin("ximagesink", "video_sink"));
    g_object_set(G_OBJECT(output_pipe.back()), "sync", FALSE, NULL);

    for (auto& p : {input_segment1, input_segment2, output_pipe}) {
        for (auto i = 1; i < p.size(); ++i) {
            assert(gst_element_link(p[i - 1], p[i]));
        }
        if (p != output_pipe) {
            assert(gst_element_link(p.back(), output_pipe.front()));
        }
    }

    auto* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_watch(bus, my_bus_callback, NULL);
    gst_object_unref(bus);
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

    g_timeout_add(switch_period, (GSourceFunc)switch_timer, video_switch);

    g_main_loop_run(loop);

    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_READY);

    /* unref */
    gst_object_unref(GST_OBJECT(pipeline));

    exit(0);
}