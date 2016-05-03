/**
 * @file ostream.h
 * @brief OStream supports emitting prediction results to some downstream
 * consumers, e.g. Mac OS Keyboard Emulator, OSC Stream, TCP Stream, etc.
 *
 * @verbatim
 * MacOSKeyboardOStream ostream(3, '\0', 'f', 'd');
 * MacOSMouseOStream ostream(3, 0, 0, 240, 240, 400, 400);
 * TcpOStream ostream("localhost", 9999, 3, "", "mouse 300, 300.", "mouse 400, 400.");
 * TcpOStream ostream("localhost", 5204, 3, "l", "r", " ");
 * @endverbatim
 *
 */
#pragma once

#include <ApplicationServices/ApplicationServices.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ofMain.h"
#include "ofApp.h"
#include "ofxTCPClient.h"
#include "stream.h"

const uint64_t kGracePeriod = 500; // 0.5 second

// Forward declaration.
class ofApp;

/**
 @brief Base class for output streams that forward ESP prediction results to
 other systems.

 To use an OStream instance in your application, pass it to useOStream() in
 your setup() function.
 */
class OStream : public virtual Stream {
  public:
    virtual void onReceive(uint32_t label) = 0;
};

/**
 @brief Emulate keyboard key presses corresponding to prediction results.
 
 This class generates a key-down then key-up command for keys corresponding
 to class labels predicted by the current pipeline. The mapping from class
 labels to keys is specified in the constructor. Note that no key press
 will be generated if less than 500 ms have elapsed since the last key press.

 To use an MacOSKeyboardOStream instance in your application, pass it to
 useOStream() in your setup() function.
 */
class MacOSKeyboardOStream : public OStream {
  public:
    /**
     @brief Create a MacOSKeyboardOStream instance, specifying the key presses
     to emulate for each predicted class label.
     
     @param key_mapping: a map from predicted class labels to keys. Note that
     class 0 is the GRT's special null prediction label and is not used to 
     generate key presses.
    */
    MacOSKeyboardOStream(std::map<uint32_t, char> key_mapping)
            : key_mapping_(key_mapping) {
    }

    /**
     @brief Create a MacOSKeyboardOStream instance, specifying the key presses
     to emulate for each predicted class label.
     
     @param count: the number of keys specified
     @param ...: the key to "press" upon prediction of the corresponding class
     label. Each key is a UTF16 character passed as an int. The first key
     specified corresponds to class label 1, the second to class label 2, etc.
    */
    MacOSKeyboardOStream(uint32_t count, ...) {
        va_list args;
        va_start(args, count);
        for (uint32_t i = 1; i <= count; i++) {
            key_mapping_[i] = va_arg(args, int);
        }
        va_end(args);
    }

    virtual void onReceive(uint32_t label) {
        if (has_started_) {
            if (getChar(label) != '\0') {
                sendKey(getChar(label));
            }
        }
    }

  private:
    void sendKey(char c) {
        if (ofGetElapsedTimeMillis() < elapsed_time_ + kGracePeriod) {
            return;
        }
        elapsed_time_ = ofGetElapsedTimeMillis();

        // Get the process number for the front application.
        ProcessSerialNumber psn = { 0, kNoProcess };
        GetFrontProcess( &psn );

        UniChar uni_char = c;
        CGEventRef key_down = CGEventCreateKeyboardEvent(NULL, 0, true);
        CGEventRef key_up = CGEventCreateKeyboardEvent(NULL, 0, false);
        CGEventKeyboardSetUnicodeString(key_down, 1, &uni_char);
        CGEventKeyboardSetUnicodeString(key_up, 1, &uni_char);
        CGEventPostToPSN(&psn, key_down);
        CGEventPostToPSN(&psn, key_up);
        CFRelease(key_down);
        CFRelease(key_up);
    }

    void sendString(const std::string& str) {
        // Get the process number for the front application.
        ProcessSerialNumber psn = { 0, kNoProcess };
        GetFrontProcess( &psn );

        UniChar s[str.length()];
        for (uint32_t i = 0; i < str.length(); i++) {
            s[i] = str[i];
        }

        CGEventRef e = CGEventCreateKeyboardEvent(NULL, 0, true);
        CGEventKeyboardSetUnicodeString(e, str.length(), s);
        CGEventPostToPSN(&psn, e);
        CFRelease(e);
    }

    char getChar(uint32_t label) {
        return key_mapping_[label];
    }

    uint64_t elapsed_time_ = 0;
    std::map<uint32_t, char> key_mapping_;
};

/**
 @brief Emulate mouse double-clicks at locations corresponding to prediction
 results.
 
 This class generates a mouse double-click at locations corresponding to class
 labels predicted by the current pipeline. The mapping from class
 labels to locations is specified in the constructor. Note that no double-click
 will be generated if less than 500 ms have elapsed since the last double-click.

 To use an MacOSMouseOStream instance in your application, pass it to
 useOStream() in your setup() function.
 */
class MacOSMouseOStream : public OStream {
  public:
    /**
     @brief Create a MacOSMouseOStream instance, specifying the locations at
     which to double-click the mouse for each predicted class label.
     
     @param mouse_mapping: a map from predicted class labels to screen
     locations (x, y pairs). Note that class 0 is the GRT's special null
     prediction label and is not used to generate mouse clicks.
    */
    MacOSMouseOStream(std::map<uint32_t, pair<uint32_t, uint32_t> > mouse_mapping)
    : mouse_mapping_(mouse_mapping) {
    }

    /**
     @brief Create a MacOSMouseOStream instance, specifying the location at
     which to double-click the mouse for each predicted class label.
     
     @param count: the number of locations specified
     @param ...: the location at which to "double-click" upon prediction of
     the corresponding class label. Each location is specified by two uint32_t
     parameters: x then y. The first location (first two parameters) specified
     corresponds to class label 1, the second (parameters 3 and 4) to class
     label 2, etc.
    */
    MacOSMouseOStream(uint32_t count, ...) {
        va_list args;
        va_start(args, count);
        for (uint32_t i = 1; i <= count; i++) {
            mouse_mapping_[i] = make_pair(va_arg(args, uint32_t),
                                          va_arg(args, uint32_t));

        }
        va_end(args);
    }

    virtual void onReceive(uint32_t label) {
        if (has_started_) {
            pair<uint32_t, uint32_t> mouse = getMousePosition(label);
            if (mouse.first > 0 & mouse.second > 0) {
                clickMouse(mouse);
            }
        }
    }

private:
    void clickMouse(pair<uint32_t, uint32_t> mouse) {
        if (ofGetElapsedTimeMillis() < elapsed_time_ + kGracePeriod) {
            return;
        }
        elapsed_time_ = ofGetElapsedTimeMillis();

        doubleClick(CGPointMake(mouse.first, mouse.second));
    }

    void doubleClick(CGPoint point, int clickCount = 2) {
        CGEventRef theEvent = CGEventCreateMouseEvent(
            NULL, kCGEventLeftMouseDown, point, kCGMouseButtonLeft);

        ProcessSerialNumber psn = { 0, kNoProcess };
        GetFrontProcess( &psn );

        CGEventSetIntegerValueField(theEvent, kCGMouseEventClickState, clickCount);
        CGEventPostToPSN(&psn, theEvent);
        CGEventSetType(theEvent, kCGEventLeftMouseUp);
        CGEventPostToPSN(&psn, theEvent);
        CGEventSetType(theEvent, kCGEventLeftMouseDown);
        CGEventPostToPSN(&psn, theEvent);
        CGEventSetType(theEvent, kCGEventLeftMouseUp);
        CGEventPostToPSN(&psn, theEvent);
        CFRelease(theEvent);
    }

    pair<uint32_t, uint32_t> getMousePosition(uint32_t label) {
        return mouse_mapping_[label];
    }

    uint64_t elapsed_time_ = 0;
    std::map<uint32_t, pair<uint32_t, uint32_t>> mouse_mapping_;
};

/**
 @brief Send strings over a TCP socket based on pipeline predictions.
 
 This class connects to a TCP server and sends it strings when predictions are
 made by the current machine learning pipeline.  The TCP connection is only
 made once, when ESP first starts, and is not restored if the other side
 disconnects.

 To use an TcpOStream instance in your application, pass it to useOStream() in
 your setup() function.
 */
class TcpOStream : public OStream {
  public:
    /**
     Create a TCPOStream instance.
     
     @param server: the hostname or IP address of the TCP server to connect to
     @param port: the port of the TCP server to connect to
     @tcp_stream_mapping: a map from predicted class labels to strings to send
     over the TCP connection. No delimiters or other characters are added to
     the strings specified, so, if they're required, be sure to include them
     in the provided strings. Note that 0 is a special GRT class label
     indicating no prediction, and will not trigger the sending of a string.
     */
    TcpOStream(string server, int port,
               std::map<uint32_t, string> tcp_stream_mapping)
            : server_(server), port_(port),
            tcp_stream_mapping_(tcp_stream_mapping) {
    }

    /**
     Create a TCPOStream instance.
     
     @param server: the hostname or IP address of the TCP server to connect to
     @param port: the port of the TCP server to connect to
     @param count: the number of strings provided
     @param ...: the strings to send over the TCP connection for each predicted
     class label. The first string provided corresponds to class label 1, the
     second to class label 2, etc. No delimiters or other characters are added
     to the strings specified, so, if they're required, be sure to include them
     in the provided strings.
     */
    TcpOStream(string server, int port, uint32_t count, ...)
            : server_(server), port_(port) {
        va_list args;
        va_start(args, count);
        for (uint32_t i = 1; i <= count; i++) {
            char* s = va_arg(args, char *);
            tcp_stream_mapping_[i] = std::string(s);
        }
        va_end(args);
    }

    virtual void onReceive(uint32_t label) {
        if (has_started_) {
            string to_send = getStreamString(label);
            if (!to_send.empty()) {
                sendString(to_send);
            }
        }
    }

    bool start() {
        has_started_ = client_.setup(server_, port_);
        client_.setMessageDelimiter("\n");
        return has_started_;
    }

private:
    void sendString(const string& tosend) {
        if (ofGetElapsedTimeMillis() < elapsed_time_ + kGracePeriod) {
            return;
        }
        elapsed_time_ = ofGetElapsedTimeMillis();

        if (client_.isConnected()) {
            client_.send(tosend);
        }
    }

    string getStreamString(uint32_t label) {
        return tcp_stream_mapping_[label];
    }

    string server_;
    int port_;
    ofxTCPClient client_;

    uint64_t elapsed_time_ = 0;
    std::map<uint32_t, string> tcp_stream_mapping_;
};

/**
 @brief Specify the OStream to use.
 
 Note that currently only one OStream is supported at a time. Subsequent calls
 to useOStream() will replace the previously-specified streams.
 
 @param stream: the OStream to use
 */
void useOStream(OStream &stream);
