/*
 * Copyright (C) 2008-2018 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#pragma once

#include "JSCJSValue.h"
#include "PropertySlot.h"
#include <wtf/Assertions.h>

namespace JSC {
    
class JSObject;
class JSFunction;
    
class PutPropertySlot {
public:
    enum Type : uint8_t { Uncachable, ExistingProperty, NewProperty, SetterProperty, CustomValue, CustomAccessor };
    enum Context { UnknownContext, PutById, PutByIdEval };
    typedef bool (*PutValueFunc)(JSGlobalObject*, EncodedJSValue thisObject, EncodedJSValue value);

    PutPropertySlot(JSValue thisValue, bool isStrictMode = false, Context context = UnknownContext, bool isInitialization = false)
        : m_base(nullptr)
        , m_thisValue(thisValue)
        , m_offset(invalidOffset)
        , m_isStrictMode(isStrictMode)
        , m_isInitialization(isInitialization)
        , m_type(Uncachable)
        , m_context(context)
        , m_cacheability(CachingAllowed)
    {
    }

    void setExistingProperty(JSObject* base, PropertyOffset offset)
    {
        m_type = ExistingProperty;
        m_base = base;
        m_offset = offset;
    }

    void setNewProperty(JSObject* base, PropertyOffset offset)
    {
        m_type = NewProperty;
        m_base = base;
        m_offset = offset;
    }

    void setCustomValue(JSObject* base, PutValueFunc function)
    {
        m_type = CustomValue;
        m_base = base;
        m_putFunction = function;
    }

    void setCustomAccessor(JSObject* base, PutValueFunc function)
    {
        m_type = CustomAccessor;
        m_base = base;
        m_putFunction = function;
    }

    void setCacheableSetter(JSObject* base, PropertyOffset offset)
    {
        m_type = SetterProperty;
        m_base = base;
        m_offset = offset;
    }

    void setThisValue(JSValue thisValue)
    {
        m_thisValue = thisValue;
    }

    void setStrictMode(bool value)
    {
        m_isStrictMode = value;
    }

    FunctionPtr<PutValuePtrTag> customSetter() const
    {
        ASSERT(isCacheableCustom());
        return m_putFunction;
    }

    Context context() const { return static_cast<Context>(m_context); }

    Type type() const { return m_type; }
    JSObject* base() const { return m_base; }
    JSValue thisValue() const { return m_thisValue; }

    bool isStrictMode() const { return m_isStrictMode; }
    bool isCacheablePut() const { return isCacheable() && (m_type == NewProperty || m_type == ExistingProperty); }
    bool isCacheableSetter() const { return isCacheable() && m_type == SetterProperty; }
    bool isCacheableCustom() const { return isCacheable() && (m_type == CustomValue || m_type == CustomAccessor) && !!m_putFunction; }
    bool isCustomAccessor() const { return isCacheable() && m_type == CustomAccessor; }
    bool isInitialization() const { return m_isInitialization; }

    PropertyOffset cachedOffset() const
    {
        return m_offset;
    }

    void disableCaching()
    {
        m_cacheability = CachingDisallowed;
    }

private:
    bool isCacheable() const { return m_cacheability == CachingAllowed; }

    JSObject* m_base;
    JSValue m_thisValue;
    PropertyOffset m_offset;
    bool m_isStrictMode : 1;
    bool m_isInitialization : 1;
    Type m_type;
    uint8_t m_context;
    CacheabilityType m_cacheability;
    FunctionPtr<PutValuePtrTag> m_putFunction;
};

} // namespace JSC
