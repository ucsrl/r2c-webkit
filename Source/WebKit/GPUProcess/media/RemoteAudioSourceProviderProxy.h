/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(GPU_PROCESS) && ENABLE(WEB_AUDIO) && PLATFORM(COCOA)

#include "Connection.h"
#include "SharedRingBufferStorage.h"
#include <WebCore/AudioSourceProviderClient.h>
#include <WebCore/CAAudioStreamDescription.h>
#include <WebCore/MediaPlayerIdentifier.h>
#include <wtf/ThreadSafeRefCounted.h>

namespace WebCore {
class AudioSourceProviderAVFObjC;
class CARingBuffer;
}

namespace WebKit {

class RemoteAudioSourceProviderProxy : public ThreadSafeRefCounted<RemoteAudioSourceProviderProxy>
    , public SharedRingBufferStorage::Client
    , public WebCore::AudioSourceProviderClient {
public:
    static Ref<RemoteAudioSourceProviderProxy> create(WebCore::MediaPlayerIdentifier, Ref<IPC::Connection>&&, WebCore::AudioSourceProviderAVFObjC&);
    ~RemoteAudioSourceProviderProxy();

    UniqueRef<WebCore::CARingBuffer> createRingBuffer(const WebCore::CAAudioStreamDescription&, size_t);
    void newAudioSamples(uint64_t startFrame, uint64_t endFrame);

private:
    RemoteAudioSourceProviderProxy(WebCore::MediaPlayerIdentifier, Ref<IPC::Connection>&&);

    // SharedRingBufferStorage::Client
    void storageChanged(SharedMemory*) final;

    // AudioSourceProviderClient
    void setFormat(size_t numberOfChannels, float sampleRate) final { }

    WebCore::MediaPlayerIdentifier m_identifier;
    Ref<IPC::Connection> m_connection;

    WebCore::CAAudioStreamDescription m_ringBufferDescription;
    size_t m_ringBufferCapacity { 0 };
};

} // namespace WebKit

#endif // ENABLE(GPU_PROCESS) && ENABLE(WEB_AUDIO) && PLATFORM(COCOA)
