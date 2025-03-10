#include "root.h"
#include "JSBuffer.h"

#include "ActiveDOMObject.h"
#include "ExtendedDOMClientIsoSubspaces.h"
#include "ExtendedDOMIsoSubspaces.h"
#include "IDLTypes.h"
// #include "JSBlob.h"
#include "JSDOMAttribute.h"
#include "JSDOMBinding.h"
#include "JSDOMConstructor.h"
#include "JSDOMConvertBase.h"
#include "JSDOMConvertInterface.h"
#include "JSDOMConvertStrings.h"
#include "JSDOMExceptionHandling.h"
#include "JSDOMGlobalObject.h"
#include "JSDOMGlobalObjectInlines.h"
#include "JSDOMOperation.h"
#include "JSDOMWrapperCache.h"
#include "ScriptExecutionContext.h"
#include "WebCoreJSClientData.h"
#include "JavaScriptCore/FunctionPrototype.h"
#include "JavaScriptCore/HeapAnalyzer.h"

#include <JavaScriptCore/JSFunction.h>
#include <JavaScriptCore/InternalFunction.h>
#include <JavaScriptCore/LazyClassStructure.h>
#include <JavaScriptCore/LazyClassStructureInlines.h>
#include <JavaScriptCore/FunctionPrototype.h>

#include "JavaScriptCore/JSDestructibleObjectHeapCellType.h"
#include "JavaScriptCore/SlotVisitorMacros.h"
#include "JavaScriptCore/SubspaceInlines.h"
#include "wtf/GetPtr.h"
#include "wtf/PointerPreparations.h"
#include "wtf/URL.h"
#include "wtf/text/WTFString.h"
#include "JavaScriptCore/BuiltinNames.h"

#include "JSBufferEncodingType.h"
#include "JavaScriptCore/JSBase.h"
#if ENABLE(MEDIA_SOURCE)
#include "BufferMediaSource.h"
#include "JSMediaSource.h"
#endif

#include <JavaScriptCore/DOMJITAbstractHeap.h>
#include "DOMJITIDLConvert.h"
#include "DOMJITIDLType.h"
#include "DOMJITIDLTypeFilter.h"
#include "DOMJITHelpers.h"
#include <JavaScriptCore/DFGAbstractHeap.h>

// #include "JavaScriptCore/JSTypedArrayViewPrototype.h"
#include "JavaScriptCore/JSArrayBufferViewInlines.h"

using namespace JSC;
using namespace WebCore;

JSC_DECLARE_HOST_FUNCTION(constructJSBuffer);

static JSC_DECLARE_HOST_FUNCTION(jsBufferConstructorFunction_alloc);
static JSC_DECLARE_HOST_FUNCTION(jsBufferConstructorFunction_allocUnsafe);
static JSC_DECLARE_HOST_FUNCTION(jsBufferConstructorFunction_allocUnsafeSlow);
static JSC_DECLARE_HOST_FUNCTION(jsBufferConstructorFunction_byteLength);
static JSC_DECLARE_HOST_FUNCTION(jsBufferConstructorFunction_compare);
static JSC_DECLARE_HOST_FUNCTION(jsBufferConstructorFunction_concat);
static JSC_DECLARE_HOST_FUNCTION(jsBufferConstructorFunction_from);
static JSC_DECLARE_HOST_FUNCTION(jsBufferConstructorFunction_isBuffer);
static JSC_DECLARE_HOST_FUNCTION(jsBufferConstructorFunction_isEncoding);
static JSC_DECLARE_HOST_FUNCTION(jsBufferConstructorFunction_toBuffer);

static JSC_DECLARE_HOST_FUNCTION(jsBufferPrototypeFunction_compare);
static JSC_DECLARE_HOST_FUNCTION(jsBufferPrototypeFunction_copy);
static JSC_DECLARE_HOST_FUNCTION(jsBufferPrototypeFunction_equals);
static JSC_DECLARE_HOST_FUNCTION(jsBufferPrototypeFunction_fill);
static JSC_DECLARE_HOST_FUNCTION(jsBufferPrototypeFunction_includes);
static JSC_DECLARE_HOST_FUNCTION(jsBufferPrototypeFunction_indexOf);
static JSC_DECLARE_HOST_FUNCTION(jsBufferPrototypeFunction_lastIndexOf);
static JSC_DECLARE_HOST_FUNCTION(jsBufferPrototypeFunction_swap16);
static JSC_DECLARE_HOST_FUNCTION(jsBufferPrototypeFunction_swap32);
static JSC_DECLARE_HOST_FUNCTION(jsBufferPrototypeFunction_swap64);
static JSC_DECLARE_HOST_FUNCTION(jsBufferPrototypeFunction_toString);
static JSC_DECLARE_HOST_FUNCTION(jsBufferPrototypeFunction_write);

static JSUint8Array* allocBuffer(JSC::JSGlobalObject* lexicalGlobalObject, size_t byteLength)
{
    JSC::VM& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);

    auto* globalObject = reinterpret_cast<Zig::GlobalObject*>(lexicalGlobalObject);
    auto* subclassStructure = globalObject->JSBufferSubclassStructure();

    auto* uint8Array = JSC::JSUint8Array::create(lexicalGlobalObject, subclassStructure, byteLength);
    if (UNLIKELY(!uint8Array)) {
        throwOutOfMemoryError(lexicalGlobalObject, throwScope);
        return nullptr;
    }

    return uint8Array;
}
static JSUint8Array* allocBufferUnsafe(JSC::JSGlobalObject* lexicalGlobalObject, size_t byteLength)
{
    JSC::VM& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);

    auto* globalObject = reinterpret_cast<Zig::GlobalObject*>(lexicalGlobalObject);
    auto* subclassStructure = globalObject->JSBufferSubclassStructure();

    auto* uint8Array = JSC::JSUint8Array::createUninitialized(lexicalGlobalObject, subclassStructure, byteLength);
    if (UNLIKELY(!uint8Array)) {
        throwOutOfMemoryError(lexicalGlobalObject, throwScope);
        return nullptr;
    }

    return uint8Array;
}

// Normalize val to be an integer in the range of [1, -1] since
// implementations of memcmp() can vary by platform.
static int normalizeCompareVal(int val, size_t a_length, size_t b_length)
{
    if (val == 0) {
        if (a_length > b_length)
            return 1;
        else if (a_length < b_length)
            return -1;
    } else {
        if (val > 0)
            return 1;
        else
            return -1;
    }
    return val;
}

static inline uint32_t parseIndex(JSC::JSGlobalObject* lexicalGlobalObject, JSC::ThrowScope& scope, JSValue arg)
{
    if (auto num = arg.tryGetAsUint32Index())
        return num.value();

    if (arg.isNumber())
        throwRangeError(lexicalGlobalObject, scope, "Invalid array length"_s);
    else
        throwTypeError(lexicalGlobalObject, scope, "Expected number"_s);

    return 0;
}

static inline WebCore::BufferEncodingType parseEncoding(JSC::JSGlobalObject* lexicalGlobalObject, JSC::ThrowScope& scope, JSValue arg)
{
    if (UNLIKELY(!arg.isString())) {
        throwTypeError(lexicalGlobalObject, scope, "Expected string"_s);
        return WebCore::BufferEncodingType::utf8;
    }

    std::optional<BufferEncodingType> encoded = parseEnumeration<BufferEncodingType>(*lexicalGlobalObject, arg);
    if (UNLIKELY(!encoded)) {
        throwTypeError(lexicalGlobalObject, scope, "Invalid encoding"_s);
        return WebCore::BufferEncodingType::utf8;
    }

    return encoded.value();
}

namespace WebCore {
using namespace JSC;

template<> class IDLOperation<JSArrayBufferView> {
public:
    using ClassParameter = JSC::JSUint8Array*;
    using Operation = JSC::EncodedJSValue(JSC::JSGlobalObject*, JSC::CallFrame*, ClassParameter);

    template<Operation operation, CastedThisErrorBehavior = CastedThisErrorBehavior::Throw>
    static JSC::EncodedJSValue call(JSC::JSGlobalObject& lexicalGlobalObject, JSC::CallFrame& callFrame, const char* operationName)
    {
        auto& vm = JSC::getVM(&lexicalGlobalObject);
        auto throwScope = DECLARE_THROW_SCOPE(vm);

        auto thisValue = callFrame.thisValue().toThis(&lexicalGlobalObject, JSC::ECMAMode::strict());
        if (thisValue.isUndefinedOrNull()) {
            throwTypeError(&lexicalGlobalObject, throwScope, "Cannot convert undefined or null to object"_s);
            return JSC::JSValue::encode(JSC::jsUndefined());
        }

        auto thisObject = JSC::jsDynamicCast<JSC::JSUint8Array*>(thisValue);
        if (UNLIKELY(!thisObject))
            return throwThisTypeError(lexicalGlobalObject, throwScope, "Buffer", operationName);

        RELEASE_AND_RETURN(throwScope, (operation(&lexicalGlobalObject, &callFrame, thisObject)));
    }
};

}

JSC::EncodedJSValue JSBuffer__bufferFromPointerAndLengthAndDeinit(JSC::JSGlobalObject* lexicalGlobalObject, char* ptr, size_t length, void* ctx, JSTypedArrayBytesDeallocator bytesDeallocator)
{

    JSC::JSUint8Array* uint8Array = nullptr;

    auto* globalObject = reinterpret_cast<Zig::GlobalObject*>(lexicalGlobalObject);
    auto* subclassStructure = globalObject->JSBufferSubclassStructure();

    if (LIKELY(length > 0)) {
        auto buffer = ArrayBuffer::createFromBytes(ptr, length, createSharedTask<void(void*)>([=](void* p) {
            if (bytesDeallocator)
                bytesDeallocator(p, ctx);
        }));

        uint8Array = JSC::JSUint8Array::create(lexicalGlobalObject, subclassStructure, WTFMove(buffer), 0, length);
    } else {
        uint8Array = JSC::JSUint8Array::create(lexicalGlobalObject, subclassStructure, 0);
    }

    return JSC::JSValue::encode(uint8Array);
}

namespace WebCore {
using namespace JSC;

static inline EncodedJSValue writeToBuffer(JSC::JSGlobalObject* lexicalGlobalObject, JSArrayBufferView* castedThis, JSString* str, uint32_t offset, uint32_t length, BufferEncodingType encoding)
{
    if (UNLIKELY(str->length() == 0))
        return JSC::JSValue::encode(JSC::jsNumber(0));

    auto view = str->tryGetValue(lexicalGlobalObject);
    size_t written = 0;

    switch (encoding) {
    case WebCore::BufferEncodingType::utf8:
    case WebCore::BufferEncodingType::latin1:
    case WebCore::BufferEncodingType::ascii:
    case WebCore::BufferEncodingType::ucs2:
    case WebCore::BufferEncodingType::utf16le:
    case WebCore::BufferEncodingType::base64:
    case WebCore::BufferEncodingType::base64url:
    case WebCore::BufferEncodingType::hex: {

        if (view.is8Bit()) {
            written = Bun__encoding__writeLatin1(view.characters8(), view.length(), reinterpret_cast<unsigned char*>(castedThis->vector()) + offset, length, static_cast<uint8_t>(encoding));
        } else {
            written = Bun__encoding__writeUTF16(view.characters16(), view.length(), reinterpret_cast<unsigned char*>(castedThis->vector()) + offset, length, static_cast<uint8_t>(encoding));
        }
        break;
    }
    }

    return JSC::JSValue::encode(JSC::jsNumber(written));
}

static inline JSC::JSUint8Array* JSBuffer__bufferFromLengthAsArray(JSC::JSGlobalObject* lexicalGlobalObject, int64_t length)
{
    auto throwScope = DECLARE_THROW_SCOPE(lexicalGlobalObject->vm());

    if (UNLIKELY(length < 0)) {
        throwRangeError(lexicalGlobalObject, throwScope, "Invalid array length"_s);
        return nullptr;
    }

    JSC::JSUint8Array* uint8Array = nullptr;

    auto* globalObject = reinterpret_cast<Zig::GlobalObject*>(lexicalGlobalObject);
    auto* subclassStructure = globalObject->JSBufferSubclassStructure();

    if (LIKELY(length > 0)) {
        uint8Array = JSC::JSUint8Array::create(lexicalGlobalObject, subclassStructure, length);
    } else {
        uint8Array = JSC::JSUint8Array::create(lexicalGlobalObject, subclassStructure, 0);
    }

    RELEASE_AND_RETURN(throwScope, uint8Array);
}

extern "C" EncodedJSValue JSBuffer__bufferFromLength(JSC::JSGlobalObject* lexicalGlobalObject, int64_t length)
{
    return JSC::JSValue::encode(JSBuffer__bufferFromLengthAsArray(lexicalGlobalObject, length));
}

static inline JSC::EncodedJSValue jsBufferConstructorFunction_allocUnsafeBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame)
{

    VM& vm = lexicalGlobalObject->vm();

    auto throwScope = DECLARE_THROW_SCOPE(vm);

    if (callFrame->argumentCount() < 1)
        return throwVMError(lexicalGlobalObject, throwScope, createNotEnoughArgumentsError(lexicalGlobalObject));

    auto length = callFrame->uncheckedArgument(0).toInt32(lexicalGlobalObject);
    RELEASE_AND_RETURN(throwScope, JSValue::encode(allocBufferUnsafe(lexicalGlobalObject, length)));
}

EncodedJSValue JSBuffer__bufferFromPointerAndLength(JSC::JSGlobalObject* lexicalGlobalObject, const unsigned char* ptr, size_t length)
{

    JSC::JSUint8Array* uint8Array;

    auto* globalObject = reinterpret_cast<Zig::GlobalObject*>(lexicalGlobalObject);
    auto* subclassStructure = globalObject->JSBufferSubclassStructure();

    if (LIKELY(length > 0)) {
        auto buffer = ArrayBuffer::create(ptr, length);

        uint8Array = JSC::JSUint8Array::create(lexicalGlobalObject, subclassStructure, WTFMove(buffer), 0, length);
    } else {
        uint8Array = JSC::JSUint8Array::create(lexicalGlobalObject, subclassStructure, 0);
    }

    return JSC::JSValue::encode(uint8Array);
}

// new Buffer()
static inline EncodedJSValue constructBufferEmpty(JSGlobalObject* lexicalGlobalObject)
{
    return JSBuffer__bufferFromLength(lexicalGlobalObject, 0);
}

// new Buffer(size)
static inline EncodedJSValue constructBufferFromLength(JSGlobalObject* lexicalGlobalObject, CallFrame* callFrame)
{
    return jsBufferConstructorFunction_allocUnsafeBody(lexicalGlobalObject, callFrame);
}

static EncodedJSValue constructFromEncoding(JSGlobalObject* lexicalGlobalObject, JSString* str, WebCore::BufferEncodingType encoding)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    auto scope = DECLARE_THROW_SCOPE(vm);

    auto view = str->tryGetValue(lexicalGlobalObject);
    JSC::EncodedJSValue result;

    if (view.is8Bit()) {
        switch (encoding) {
        case WebCore::BufferEncodingType::utf8:
        case WebCore::BufferEncodingType::ucs2:
        case WebCore::BufferEncodingType::utf16le:
        case WebCore::BufferEncodingType::base64:
        case WebCore::BufferEncodingType::base64url:
        case WebCore::BufferEncodingType::hex: {
            result = Bun__encoding__constructFromLatin1(lexicalGlobalObject, view.characters8(), view.length(), static_cast<uint8_t>(encoding));
            break;
        }
        case WebCore::BufferEncodingType::ascii: // ascii is a noop for latin1
        case WebCore::BufferEncodingType::latin1: { // The native encoding is latin1, so we don't need to do any conversion.
            result = JSBuffer__bufferFromPointerAndLength(lexicalGlobalObject, view.characters8(), view.length());
            break;
        }
        }
    } else {
        switch (encoding) {
        case WebCore::BufferEncodingType::utf8:
        case WebCore::BufferEncodingType::base64:
        case WebCore::BufferEncodingType::base64url:
        case WebCore::BufferEncodingType::ascii:
        case WebCore::BufferEncodingType::latin1: {
            result = Bun__encoding__constructFromUTF16(lexicalGlobalObject, view.characters16(), view.length(), static_cast<uint8_t>(encoding));
            break;
        }
        case WebCore::BufferEncodingType::ucs2:
        case WebCore::BufferEncodingType::utf16le: {
            // The native encoding is UTF-16
            // so we don't need to do any conversion.
            result = JSBuffer__bufferFromPointerAndLength(lexicalGlobalObject, reinterpret_cast<const unsigned char*>(view.characters16()), view.length() * 2);
            break;
        }
        }
    }

    JSC::JSValue decoded = JSC::JSValue::decode(result);
    if (UNLIKELY(!result)) {
        throwTypeError(lexicalGlobalObject, scope, "An error occurred while decoding the string"_s);
        return JSC::JSValue::encode(jsUndefined());
    }

    if (decoded.isCell() && decoded.getObject()->isErrorInstance()) {
        scope.throwException(lexicalGlobalObject, decoded);
        return JSC::JSValue::encode(jsUndefined());
    }
    return result;
}

static inline JSC::EncodedJSValue constructBufferFromStringAndEncoding(JSC::JSGlobalObject* lexicalGlobalObject, JSValue arg0, JSValue arg1)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    uint32_t offset = 0;
    WebCore::BufferEncodingType encoding = WebCore::BufferEncodingType::utf8;

    auto scope = DECLARE_THROW_SCOPE(vm);

    auto* str = arg0.toString(lexicalGlobalObject);

    RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));

    if (arg1 && arg1.isString()) {
        std::optional<BufferEncodingType> encoded = parseEnumeration<BufferEncodingType>(*lexicalGlobalObject, arg1);
        if (!encoded) {
            throwTypeError(lexicalGlobalObject, scope, "Invalid encoding"_s);
            return JSC::JSValue::encode(jsUndefined());
        }

        encoding = encoded.value();
    }

    if (str->length() == 0)
        return constructBufferEmpty(lexicalGlobalObject);

    JSC::EncodedJSValue result = constructFromEncoding(lexicalGlobalObject, str, encoding);

    RELEASE_AND_RETURN(scope, result);
}

static inline JSC::EncodedJSValue jsBufferConstructorFunction_allocBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame)
{
    VM& vm = lexicalGlobalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    auto lengthArg = callFrame->uncheckedArgument(0);
    if (UNLIKELY(!lengthArg.isNumber())) {
        throwTypeError(lexicalGlobalObject, scope, "Expected number"_s);
        return JSValue::encode(jsUndefined());
    }
    auto length = lengthArg.toInt32(lexicalGlobalObject);
    if (length < 0) {
        throwRangeError(lexicalGlobalObject, scope, "Invalid array length"_s);
        return JSValue::encode(jsUndefined());
    }

    auto* globalObject = reinterpret_cast<Zig::GlobalObject*>(lexicalGlobalObject);
    auto* subclassStructure = globalObject->JSBufferSubclassStructure();

    // fill argument
    if (UNLIKELY(callFrame->argumentCount() > 1)) {
        auto* uint8Array = JSC::JSUint8Array::createUninitialized(lexicalGlobalObject, subclassStructure, length);
        auto value = callFrame->argument(1);

        if (value.isString()) {
            size_t length = uint8Array->byteLength();
            size_t start = 0;
            size_t end = length;
            WebCore::BufferEncodingType encoding = WebCore::BufferEncodingType::utf8;
            if (callFrame->argumentCount() > 2) {
                EnsureStillAliveScope arg2 = callFrame->uncheckedArgument(2);
                if (!arg2.value().isUndefined()) {
                    encoding = parseEncoding(lexicalGlobalObject, scope, arg2.value());
                    RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));
                }
            }
            auto startPtr = uint8Array->typedVector() + start;
            auto str_ = value.toWTFString(lexicalGlobalObject);
            ZigString str = Zig::toZigString(str_);

            if (UNLIKELY(!Bun__Buffer_fill(&str, startPtr, end - start, encoding))) {
                throwTypeError(lexicalGlobalObject, scope, "Failed to decode value"_s);
                return JSC::JSValue::encode(jsUndefined());
            }
        } else if (auto* view = JSC::jsDynamicCast<JSC::JSArrayBufferView*>(value)) {
            if (UNLIKELY(view->isDetached())) {
                throwVMTypeError(lexicalGlobalObject, scope, "Uint8Array is detached"_s);
                return JSValue::encode(jsUndefined());
            }

            size_t length = view->byteLength();
            if (UNLIKELY(length == 0)) {
                throwTypeError(lexicalGlobalObject, scope, "Buffer cannot be empty"_s);
                return JSC::JSValue::encode(jsUndefined());
            }

            auto* start = uint8Array->typedVector();
            auto* head = start;
            size_t remain = uint8Array->byteLength();
            memmove(head, view->vector(), length);
            remain -= length;
            head += length;
            while (remain >= length) {
                memcpy(head, start, length);
                remain -= length;
                head += length;
                length <<= 1;
            }
            if (remain > 0) {
                memcpy(head, start, remain);
            }
        } else {
            auto value_ = value.toInt32(lexicalGlobalObject) & 0xFF;

            auto value_uint8 = static_cast<uint8_t>(value_);
            RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));

            auto length = uint8Array->byteLength();
            auto start = 0;
            auto end = length;

            auto startPtr = uint8Array->typedVector() + start;
            auto endPtr = uint8Array->typedVector() + end;
            memset(startPtr, value_uint8, endPtr - startPtr);
        }

        RELEASE_AND_RETURN(scope, JSC::JSValue::encode(uint8Array));
    } else {
        auto* uint8Array = JSC::JSUint8Array::create(lexicalGlobalObject, subclassStructure, length);
        if (UNLIKELY(!uint8Array)) {
            throwOutOfMemoryError(lexicalGlobalObject, scope);
            return JSC::JSValue::encode(jsUndefined());
        }

        RELEASE_AND_RETURN(scope, JSC::JSValue::encode(uint8Array));
    }
}

static inline JSC::EncodedJSValue jsBufferConstructorFunction_allocUnsafeSlowBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame)
{
    return jsBufferConstructorFunction_allocUnsafeBody(lexicalGlobalObject, callFrame);
}

// new SlowBuffer(size)
EncodedJSValue constructSlowBuffer(JSGlobalObject* lexicalGlobalObject, CallFrame* callFrame)
{
    return jsBufferConstructorFunction_allocUnsafeSlowBody(lexicalGlobalObject, callFrame);
}

static inline JSC::EncodedJSValue jsBufferByteLengthFromStringAndEncoding(JSC::JSGlobalObject* lexicalGlobalObject, JSString* str, WebCore::BufferEncodingType encoding)
{
    auto scope = DECLARE_THROW_SCOPE(lexicalGlobalObject->vm());
    if (!str) {
        throwTypeError(lexicalGlobalObject, scope, "byteLength() expects a string"_s);
        return JSC::JSValue::encode(jsUndefined());
    }

    if (str->length() == 0)
        RELEASE_AND_RETURN(scope, JSC::JSValue::encode(JSC::jsNumber(0)));

    int64_t written = 0;

    switch (encoding) {

    case WebCore::BufferEncodingType::ucs2:
    case WebCore::BufferEncodingType::utf16le: {
        // https://github.com/nodejs/node/blob/e676942f814915b2d24fc899bb42dc71ae6c8226/lib/buffer.js#L600
        RELEASE_AND_RETURN(scope, JSC::JSValue::encode(JSC::jsNumber(str->length() * 2)));
    }

    case WebCore::BufferEncodingType::latin1:
    case WebCore::BufferEncodingType::ascii: {
        // https://github.com/nodejs/node/blob/e676942f814915b2d24fc899bb42dc71ae6c8226/lib/buffer.js#L627
        RELEASE_AND_RETURN(scope, JSC::JSValue::encode(JSC::jsNumber(str->length())));
    }

    case WebCore::BufferEncodingType::base64:
    case WebCore::BufferEncodingType::base64url: {
        int64_t length = str->length();
        auto view = str->tryGetValue(lexicalGlobalObject);

        if (view.is8Bit()) {
            if (view.characters8()[length - 1] == 0x3D) {
                length--;

                if (length > 1 && view.characters8()[length - 1] == '=')
                    length--;
            }
        } else {
            if (view.characters16()[length - 1] == 0x3D) {
                length--;

                if (length > 1 && view.characters16()[length - 1] == '=')
                    length--;
            }
        }

        // https://github.com/nodejs/node/blob/e676942f814915b2d24fc899bb42dc71ae6c8226/lib/buffer.js#L579
        return JSValue::encode(jsNumber(static_cast<double>((length * 3) >> 2)));
    }

    case WebCore::BufferEncodingType::hex: {
        return JSValue::encode(jsNumber(str->length() >> 1));
    }

    case WebCore::BufferEncodingType::utf8: {
        auto view = str->tryGetValue(lexicalGlobalObject);
        if (view.is8Bit()) {
            written = Bun__encoding__byteLengthLatin1(view.characters8(), view.length(), static_cast<uint8_t>(encoding));
        } else {
            written = Bun__encoding__byteLengthUTF16(view.characters16(), view.length(), static_cast<uint8_t>(encoding));
        }
        break;
    }
    }

    RELEASE_AND_RETURN(scope, JSC::JSValue::encode(JSC::jsNumber(written)));
}
static inline JSC::EncodedJSValue jsBufferConstructorFunction_byteLengthBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);

    uint32_t offset = 0;
    WebCore::BufferEncodingType encoding = WebCore::BufferEncodingType::utf8;

    auto scope = DECLARE_THROW_SCOPE(vm);

    if (UNLIKELY(callFrame->argumentCount() == 0)) {
        throwTypeError(lexicalGlobalObject, scope, "Not enough arguments"_s);
        return JSC::JSValue::encode(jsUndefined());
    }

    EnsureStillAliveScope arg0 = callFrame->argument(0);

    EnsureStillAliveScope arg1 = callFrame->argument(1);
    if (callFrame->argumentCount() > 1) {

        if (arg1.value().isString()) {
            std::optional<BufferEncodingType> encoded = parseEnumeration<BufferEncodingType>(*lexicalGlobalObject, arg1.value());

            // this one doesn't fail
            if (encoded) {
                encoding = encoded.value();
            }
        }
    }

    if (LIKELY(arg0.value().isString()))
        return jsBufferByteLengthFromStringAndEncoding(lexicalGlobalObject, asString(arg0.value()), encoding);

    if (auto* arrayBufferView = jsDynamicCast<JSC::JSArrayBufferView*>(arg0.value())) {
        return JSValue::encode(jsNumber(arrayBufferView->byteLength()));
    }

    if (auto* arrayBuffer = jsDynamicCast<JSC::JSArrayBuffer*>(arg0.value())) {
        return JSValue::encode(jsNumber(arrayBuffer->impl()->byteLength()));
    }

    throwTypeError(lexicalGlobalObject, scope, "Invalid input, must be a string, Buffer, or ArrayBuffer"_s);
    return JSC::JSValue::encode(jsUndefined());
}

static inline JSC::EncodedJSValue jsBufferConstructorFunction_compareBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    if (callFrame->argumentCount() < 2) {
        throwVMError(lexicalGlobalObject, throwScope, createNotEnoughArgumentsError(lexicalGlobalObject));
        return JSValue::encode(jsUndefined());
    }

    auto castedThisValue = callFrame->uncheckedArgument(0);
    JSC::JSArrayBufferView* castedThis = JSC::jsDynamicCast<JSC::JSArrayBufferView*>(castedThisValue);
    if (UNLIKELY(!castedThis)) {
        throwVMTypeError(lexicalGlobalObject, throwScope, "Expected Buffer (first argument)"_s);
        return JSValue::encode(jsUndefined());
    }
    if (UNLIKELY(castedThis->isDetached())) {
        throwVMTypeError(lexicalGlobalObject, throwScope, "Uint8Array (first argument) is detached"_s);
        return JSValue::encode(jsUndefined());
    }

    auto buffer = callFrame->uncheckedArgument(1);
    JSC::JSArrayBufferView* view = JSC::jsDynamicCast<JSC::JSArrayBufferView*>(buffer);
    if (UNLIKELY(!view)) {
        throwVMTypeError(lexicalGlobalObject, throwScope, "Expected Buffer (2nd argument)"_s);
        return JSValue::encode(jsUndefined());
    }
    if (UNLIKELY(view->isDetached())) {
        throwVMTypeError(lexicalGlobalObject, throwScope, "Uint8Array (second argument) is detached"_s);
        return JSValue::encode(jsUndefined());
    }

    size_t targetStart = 0;
    size_t targetEndInit = view->byteLength();
    size_t targetEnd = targetEndInit;

    size_t sourceStart = 0;
    size_t sourceEndInit = castedThis->byteLength();
    size_t sourceEnd = sourceEndInit;

    switch (callFrame->argumentCount()) {
    default:
        sourceEnd = parseIndex(lexicalGlobalObject, throwScope, callFrame->uncheckedArgument(5));
        RETURN_IF_EXCEPTION(throwScope, JSValue::encode(jsUndefined()));
        FALLTHROUGH;
    case 5:
        sourceStart = parseIndex(lexicalGlobalObject, throwScope, callFrame->uncheckedArgument(4));
        RETURN_IF_EXCEPTION(throwScope, JSValue::encode(jsUndefined()));
        FALLTHROUGH;
    case 4:
        targetEnd = parseIndex(lexicalGlobalObject, throwScope, callFrame->uncheckedArgument(3));
        RETURN_IF_EXCEPTION(throwScope, JSValue::encode(jsUndefined()));
        FALLTHROUGH;
    case 3:
        targetStart = parseIndex(lexicalGlobalObject, throwScope, callFrame->uncheckedArgument(2));
        RETURN_IF_EXCEPTION(throwScope, JSValue::encode(jsUndefined()));
        break;
    case 2:
    case 1:
    case 0:
        break;
    }

    targetStart = std::min(targetStart, std::min(targetEnd, targetEndInit));
    sourceStart = std::min(sourceStart, std::min(sourceEnd, sourceEndInit));

    auto sourceLength = sourceEnd - sourceStart;
    auto targetLength = targetEnd - targetStart;
    auto actualLength = std::min(sourceLength, targetLength);

    auto sourceStartPtr = reinterpret_cast<unsigned char*>(castedThis->vector()) + sourceStart;
    auto targetStartPtr = reinterpret_cast<unsigned char*>(view->vector()) + targetStart;

    auto result = actualLength > 0 ? memcmp(sourceStartPtr, targetStartPtr, actualLength) : 0;

    RELEASE_AND_RETURN(throwScope, JSC::JSValue::encode(JSC::jsNumber(normalizeCompareVal(result, sourceLength, targetLength))));
}
static inline JSC::EncodedJSValue jsBufferConstructorFunction_concatBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);

    auto throwScope = DECLARE_THROW_SCOPE(vm);
    if (callFrame->argumentCount() < 1) {
        return constructBufferEmpty(lexicalGlobalObject);
    }

    auto arrayValue = callFrame->uncheckedArgument(0);
    auto array = JSC::jsDynamicCast<JSC::JSArray*>(arrayValue);
    if (!array) {
        throwTypeError(lexicalGlobalObject, throwScope, "Argument must be an array"_s);
        return JSValue::encode(jsUndefined());
    }

    size_t arrayLength = array->length();
    if (arrayLength < 1) {
        RELEASE_AND_RETURN(throwScope, constructBufferEmpty(lexicalGlobalObject));
    }

    size_t byteLength = 0;

    for (size_t i = 0; i < arrayLength; i++) {
        auto element = array->getIndex(lexicalGlobalObject, i);
        RETURN_IF_EXCEPTION(throwScope, {});

        auto* typedArray = JSC::jsDynamicCast<JSC::JSUint8Array*>(element);
        if (!typedArray) {
            throwTypeError(lexicalGlobalObject, throwScope, "Buffer.concat expects Uint8Array"_s);
            return JSValue::encode(jsUndefined());
        }
        byteLength += typedArray->length();
    }

    if (callFrame->argumentCount() > 1) {
        auto byteLengthValue = callFrame->uncheckedArgument(1);
        byteLength = std::min(byteLength, byteLengthValue.toTypedArrayIndex(lexicalGlobalObject, "totalLength must be a valid number"_s));
        RETURN_IF_EXCEPTION(throwScope, {});
    }

    if (byteLength == 0) {
        RELEASE_AND_RETURN(throwScope, constructBufferEmpty(lexicalGlobalObject));
    }

    JSC::JSUint8Array* outBuffer = JSBuffer__bufferFromLengthAsArray(lexicalGlobalObject, byteLength);
    size_t remain = byteLength;
    auto* head = outBuffer->typedVector();

    for (size_t i = 0; i < arrayLength && remain > 0; i++) {
        auto element = array->getIndex(lexicalGlobalObject, i);
        RETURN_IF_EXCEPTION(throwScope, {});
        auto* typedArray = JSC::jsCast<JSC::JSUint8Array*>(element);
        size_t length = std::min(remain, typedArray->length());
        memcpy(head, typedArray->typedVector(), length);
        remain -= length;
        head += length;
    }

    RELEASE_AND_RETURN(throwScope, JSC::JSValue::encode(JSC::JSValue(outBuffer)));
}

static inline JSC::EncodedJSValue jsBufferConstructorFunction_isEncodingBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    auto* encoding_ = callFrame->argument(0).toStringOrNull(lexicalGlobalObject);
    if (!encoding_)
        return JSValue::encode(jsBoolean(false));

    std::optional<BufferEncodingType> encoded = parseEnumeration<BufferEncodingType>(*lexicalGlobalObject, encoding_);
    return JSValue::encode(jsBoolean(!!encoded));
}

static inline JSC::EncodedJSValue jsBufferConstructorFunction_toBufferBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    if (UNLIKELY(callFrame->argumentCount() < 1)) {
        throwVMError(lexicalGlobalObject, throwScope, createNotEnoughArgumentsError(lexicalGlobalObject));
        return JSValue::encode(jsUndefined());
    }

    auto buffer = callFrame->uncheckedArgument(0);
    if (!buffer.isCell() || !JSC::isTypedView(buffer.asCell()->type())) {
        throwVMTypeError(lexicalGlobalObject, throwScope, "Expected Uint8Array"_s);
        return JSValue::encode(jsUndefined());
    }

    JSC::JSUint8Array* view = JSC::jsDynamicCast<JSC::JSUint8Array*>(buffer);

    if (!view) {
        throwVMTypeError(lexicalGlobalObject, throwScope, "Expected Uint8Array"_s);
        return JSValue::encode(jsUndefined());
    }
    toBuffer(lexicalGlobalObject, view);
    RELEASE_AND_RETURN(throwScope, JSC::JSValue::encode(view));
}

class JSBufferPrototype : public JSC::JSNonFinalObject {
public:
    using Base = JSC::JSNonFinalObject;
    static JSBufferPrototype* create(JSC::VM& vm, JSGlobalObject* globalObject, JSC::Structure* structure)
    {
        JSBufferPrototype* ptr = new (NotNull, JSC::allocateCell<JSBufferPrototype>(vm)) JSBufferPrototype(vm, globalObject, structure);
        ptr->finishCreation(vm, globalObject);
        return ptr;
    }

    DECLARE_INFO;
    template<typename CellType, JSC::SubspaceAccess>
    static JSC::GCClient::IsoSubspace* subspaceFor(JSC::VM& vm)
    {
        return &vm.plainObjectSpace();
    }
    static JSC::Structure* createStructure(JSC::VM& vm, JSC::JSGlobalObject* globalObject, JSC::JSValue prototype)
    {
        return JSC::Structure::create(vm, globalObject, prototype, JSC::TypeInfo(JSC::ObjectType, StructureFlags), info());
    }

private:
    JSBufferPrototype(JSC::VM& vm, JSC::JSGlobalObject* globalObject, JSC::Structure* structure)
        : Base(vm, structure)
    {
    }

    void finishCreation(JSC::VM&, JSC::JSGlobalObject*);
};
STATIC_ASSERT_ISO_SUBSPACE_SHARABLE(JSBufferPrototype, JSBufferPrototype::Base);

static inline JSC::EncodedJSValue jsBufferPrototypeFunction_compareBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSArrayBufferView>::ClassParameter castedThis)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    if (callFrame->argumentCount() < 1) {
        throwVMError(lexicalGlobalObject, throwScope, createNotEnoughArgumentsError(lexicalGlobalObject));
        return JSValue::encode(jsUndefined());
    }

    JSC::JSUint8Array* view = JSC::jsDynamicCast<JSC::JSUint8Array*>(callFrame->uncheckedArgument(0));

    if (UNLIKELY(!view)) {
        throwVMTypeError(lexicalGlobalObject, throwScope, "Expected Uint8Array"_s);
        return JSValue::encode(jsUndefined());
    }

    if (UNLIKELY(view->isDetached())) {
        throwVMTypeError(lexicalGlobalObject, throwScope, "Uint8Array is detached"_s);
        return JSValue::encode(jsUndefined());
    }

    size_t targetStart = 0;
    size_t targetEndInit = view->byteLength();
    size_t targetEnd = targetEndInit;

    size_t sourceStart = 0;
    size_t sourceEndInit = castedThis->byteLength();
    size_t sourceEnd = sourceEndInit;

    switch (callFrame->argumentCount()) {
    default:
        sourceEnd = parseIndex(lexicalGlobalObject, throwScope, callFrame->uncheckedArgument(4));
        RETURN_IF_EXCEPTION(throwScope, JSValue::encode(jsUndefined()));
        FALLTHROUGH;
    case 4:
        sourceStart = parseIndex(lexicalGlobalObject, throwScope, callFrame->uncheckedArgument(3));
        RETURN_IF_EXCEPTION(throwScope, JSValue::encode(jsUndefined()));
        FALLTHROUGH;
    case 3:
        targetEnd = parseIndex(lexicalGlobalObject, throwScope, callFrame->uncheckedArgument(2));
        RETURN_IF_EXCEPTION(throwScope, JSValue::encode(jsUndefined()));
        FALLTHROUGH;
    case 2:
        targetStart = parseIndex(lexicalGlobalObject, throwScope, callFrame->uncheckedArgument(1));
        RETURN_IF_EXCEPTION(throwScope, JSValue::encode(jsUndefined()));
        break;
    case 1:
    case 0:
        break;
    }

    targetStart = std::min(targetStart, std::min(targetEnd, targetEndInit));
    sourceStart = std::min(sourceStart, std::min(sourceEnd, sourceEndInit));

    auto sourceLength = sourceEnd - sourceStart;
    auto targetLength = targetEnd - targetStart;
    auto actualLength = std::min(sourceLength, targetLength);

    auto sourceStartPtr = castedThis->typedVector() + sourceStart;
    auto targetStartPtr = view->typedVector() + targetStart;

    auto result = actualLength > 0 ? memcmp(sourceStartPtr, targetStartPtr, actualLength) : 0;

    RELEASE_AND_RETURN(throwScope, JSC::JSValue::encode(JSC::jsNumber(normalizeCompareVal(result, sourceLength, targetLength))));
}
static inline JSC::EncodedJSValue jsBufferPrototypeFunction_copyBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSArrayBufferView>::ClassParameter castedThis)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);

    if (callFrame->argumentCount() < 1) {
        throwVMError(lexicalGlobalObject, throwScope, createNotEnoughArgumentsError(lexicalGlobalObject));
        return JSValue::encode(jsUndefined());
    }

    auto buffer = callFrame->uncheckedArgument(0);

    if (!buffer.isCell() || !JSC::isTypedView(buffer.asCell()->type())) {
        throwVMTypeError(lexicalGlobalObject, throwScope, "Expected Uint8Array"_s);
        return JSValue::encode(jsUndefined());
    }

    JSC::JSArrayBufferView* view = JSC::jsDynamicCast<JSC::JSArrayBufferView*>(buffer);
    if (UNLIKELY(!view || view->isDetached())) {
        throwVMTypeError(lexicalGlobalObject, throwScope, "Uint8Array is detached"_s);
        return JSValue::encode(jsUndefined());
    }

    size_t targetStart = 0;
    size_t targetEnd = view->byteLength();

    size_t sourceStart = 0;
    size_t sourceEndInit = castedThis->byteLength();
    size_t sourceEnd = sourceEndInit;

    switch (callFrame->argumentCount()) {
    default:
        sourceEnd = parseIndex(lexicalGlobalObject, throwScope, callFrame->uncheckedArgument(3));
        RETURN_IF_EXCEPTION(throwScope, JSValue::encode(jsUndefined()));
        FALLTHROUGH;
    case 3:
        sourceStart = parseIndex(lexicalGlobalObject, throwScope, callFrame->uncheckedArgument(2));
        RETURN_IF_EXCEPTION(throwScope, JSValue::encode(jsUndefined()));
        FALLTHROUGH;
    case 2:
        targetStart = parseIndex(lexicalGlobalObject, throwScope, callFrame->uncheckedArgument(1));
        RETURN_IF_EXCEPTION(throwScope, JSValue::encode(jsUndefined()));
        break;
    case 1:
    case 0:
        break;
    }

    targetStart = std::min(targetStart, targetEnd);
    sourceStart = std::min(sourceStart, std::min(sourceEnd, sourceEndInit));

    auto sourceLength = sourceEnd - sourceStart;
    auto targetLength = targetEnd - targetStart;
    auto actualLength = std::min(sourceLength, targetLength);

    auto sourceStartPtr = castedThis->typedVector() + sourceStart;
    auto targetStartPtr = reinterpret_cast<unsigned char*>(view->vector()) + targetStart;

    if (actualLength > 0)
        memmove(targetStartPtr, sourceStartPtr, actualLength);

    return JSValue::encode(jsNumber(actualLength));
}

static inline JSC::EncodedJSValue jsBufferPrototypeFunction_equalsBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSArrayBufferView>::ClassParameter castedThis)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    if (callFrame->argumentCount() < 1) {
        throwVMError(lexicalGlobalObject, throwScope, createNotEnoughArgumentsError(lexicalGlobalObject));
        return JSValue::encode(jsUndefined());
    }

    auto buffer = callFrame->uncheckedArgument(0);
    JSC::JSArrayBufferView* view = JSC::jsDynamicCast<JSC::JSArrayBufferView*>(buffer);
    if (UNLIKELY(!view)) {
        throwVMTypeError(lexicalGlobalObject, throwScope, "Expected Buffer"_s);
        return JSValue::encode(jsUndefined());
    }

    if (UNLIKELY(view->isDetached())) {
        throwVMTypeError(lexicalGlobalObject, throwScope, "Uint8Array is detached"_s);
        return JSValue::encode(jsUndefined());
    }

    size_t a_length = castedThis->byteLength();
    size_t b_length = view->byteLength();
    auto sourceStartPtr = castedThis->typedVector();
    auto targetStartPtr = reinterpret_cast<unsigned char*>(view->vector());

    // same pointer, same length, same contents
    if (sourceStartPtr == targetStartPtr && a_length == b_length)
        RELEASE_AND_RETURN(throwScope, JSValue::encode(jsBoolean(true)));

    size_t compare_length = std::min(a_length, b_length);
    auto result = compare_length > 0 ? memcmp(sourceStartPtr, targetStartPtr, compare_length) : 0;

    RELEASE_AND_RETURN(throwScope, JSC::JSValue::encode(JSC::jsBoolean(normalizeCompareVal(result, a_length, b_length) == 0)));
}
static inline JSC::EncodedJSValue jsBufferPrototypeFunction_fillBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSArrayBufferView>::ClassParameter castedThis)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (callFrame->argumentCount() < 1) {
        return JSValue::encode(castedThis);
    }

    auto value = callFrame->uncheckedArgument(0);
    const size_t limit = castedThis->byteLength();
    size_t start = 0;
    size_t end = limit;
    WebCore::BufferEncodingType encoding = WebCore::BufferEncodingType::utf8;
    JSValue encodingValue = jsUndefined();
    JSValue offsetValue = jsUndefined();
    JSValue lengthValue = jsUndefined();

    switch (callFrame->argumentCount()) {
    case 4:
        encodingValue = callFrame->uncheckedArgument(3);
        FALLTHROUGH;
    case 3:
        lengthValue = callFrame->uncheckedArgument(2);
        FALLTHROUGH;
    case 2:
        offsetValue = callFrame->uncheckedArgument(1);
        FALLTHROUGH;
    default:
        break;
    }

    if (offsetValue.isUndefined() || offsetValue.isString()) {
        encodingValue = offsetValue;
        offsetValue = jsUndefined();
    } else if (lengthValue.isString()) {
        encodingValue = lengthValue;
        lengthValue = jsUndefined();
    }

    if (!encodingValue.isUndefined()) {
        encoding = parseEncoding(lexicalGlobalObject, scope, encodingValue);
        RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));
    }

    if (!offsetValue.isUndefined()) {
        start = parseIndex(lexicalGlobalObject, scope, offsetValue);
        RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));
    }

    if (!lengthValue.isUndefined()) {
        end = parseIndex(lexicalGlobalObject, scope, lengthValue);
        RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));
    }

    if (start >= end) {
        RELEASE_AND_RETURN(scope, JSValue::encode(castedThis));
    }

    if (UNLIKELY(end > limit)) {
        throwRangeError(lexicalGlobalObject, scope, "end out of range"_s);
        return JSC::JSValue::encode(jsUndefined());
    }

    if (value.isString()) {
        auto startPtr = castedThis->typedVector() + start;
        auto str_ = value.toWTFString(lexicalGlobalObject);
        ZigString str = Zig::toZigString(str_);

        if (str.len == 0) {
            memset(startPtr, 0, end - start);
        } else if (UNLIKELY(!Bun__Buffer_fill(&str, startPtr, end - start, encoding))) {
            throwTypeError(lexicalGlobalObject, scope, "Failed to decode value"_s);
            return JSC::JSValue::encode(jsUndefined());
        }
    } else if (auto* view = JSC::jsDynamicCast<JSC::JSArrayBufferView*>(value)) {
        auto* startPtr = castedThis->typedVector() + start;
        auto* head = startPtr;
        size_t remain = end - start;

        if (UNLIKELY(view->isDetached())) {
            throwVMTypeError(lexicalGlobalObject, scope, "Uint8Array is detached"_s);
            return JSValue::encode(jsUndefined());
        }

        size_t length = view->byteLength();
        if (UNLIKELY(length == 0)) {
            throwTypeError(lexicalGlobalObject, scope, "Buffer cannot be empty"_s);
            return JSC::JSValue::encode(jsUndefined());
        }

        memmove(head, view->vector(), length);
        remain -= length;
        head += length;
        while (remain >= length) {
            memcpy(head, startPtr, length);
            remain -= length;
            head += length;
            length <<= 1;
        }
        if (remain > 0) {
            memcpy(head, startPtr, remain);
        }
    } else {
        auto value_ = value.toInt32(lexicalGlobalObject) & 0xFF;

        auto value_uint8 = static_cast<uint8_t>(value_);
        RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));

        auto startPtr = castedThis->typedVector() + start;
        auto endPtr = castedThis->typedVector() + end;
        memset(startPtr, value_uint8, endPtr - startPtr);
    }

    RELEASE_AND_RETURN(scope, JSValue::encode(castedThis));
}

static int64_t indexOf(const uint8_t* thisPtr, int64_t thisLength, const uint8_t* valuePtr, int64_t valueLength, int64_t byteOffset)
{
    if (thisLength < valueLength + byteOffset)
        return -1;
    auto start = thisPtr + byteOffset;
    auto it = static_cast<uint8_t*>(memmem(start, static_cast<size_t>(thisLength - byteOffset), valuePtr, static_cast<size_t>(valueLength)));
    if (it != NULL) {
        return it - thisPtr;
    }
    return -1;
}

static int64_t lastIndexOf(const uint8_t* thisPtr, int64_t thisLength, const uint8_t* valuePtr, int64_t valueLength, int64_t byteOffset)
{
    auto start = thisPtr;
    auto end = thisPtr + std::min(thisLength, byteOffset + valueLength);
    auto it = std::find_end(start, end, valuePtr, valuePtr + valueLength);
    if (it != end) {
        return it - thisPtr;
    }
    return -1;
}

static int64_t indexOf(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSArrayBufferView>::ClassParameter castedThis, bool last)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (callFrame->argumentCount() < 1) {
        throwVMError(lexicalGlobalObject, scope, createNotEnoughArgumentsError(lexicalGlobalObject));
        return JSValue::encode(jsUndefined());
    }

    auto value = callFrame->uncheckedArgument(0);
    WebCore::BufferEncodingType encoding = WebCore::BufferEncodingType::utf8;

    int64_t length = static_cast<int64_t>(castedThis->byteLength());
    const uint8_t* typedVector = castedThis->typedVector();

    int64_t byteOffset = last ? length - 1 : 0;

    if (callFrame->argumentCount() > 1) {
        EnsureStillAliveScope arg1 = callFrame->uncheckedArgument(1);
        if (arg1.value().isString()) {
            encoding = parseEncoding(lexicalGlobalObject, scope, arg1.value());
            RETURN_IF_EXCEPTION(scope, -1);
        } else {
            auto byteOffset_ = arg1.value().toNumber(lexicalGlobalObject);
            RETURN_IF_EXCEPTION(scope, -1);

            if (std::isnan(byteOffset_) || std::isinf(byteOffset_)) {
                byteOffset = last ? length - 1 : 0;
            } else if (byteOffset_ < 0) {
                byteOffset = length + static_cast<int64_t>(byteOffset_);
            } else {
                byteOffset = static_cast<int64_t>(byteOffset_);
            }

            if (last) {
                if (byteOffset < 0) {
                    return -1;
                } else if (byteOffset > length - 1) {
                    byteOffset = length - 1;
                }
            } else {
                if (byteOffset <= 0) {
                    byteOffset = 0;
                } else if (byteOffset > length - 1) {
                    return -1;
                }
            }

            if (callFrame->argumentCount() > 2) {
                EnsureStillAliveScope encodingValue = callFrame->uncheckedArgument(2);
                if (!encodingValue.value().isUndefined()) {
                    encoding = parseEncoding(lexicalGlobalObject, scope, encodingValue.value());
                    RETURN_IF_EXCEPTION(scope, -1);
                }
            }
        }
    }

    if (value.isString()) {
        auto* str = value.toStringOrNull(lexicalGlobalObject);
        RETURN_IF_EXCEPTION(scope, -1);

        JSC::EncodedJSValue encodedBuffer = constructFromEncoding(lexicalGlobalObject, str, encoding);
        auto* arrayValue = JSC::jsDynamicCast<JSC::JSUint8Array*>(JSC::JSValue::decode(encodedBuffer));
        int64_t lengthValue = static_cast<int64_t>(arrayValue->byteLength());
        const uint8_t* typedVectorValue = arrayValue->typedVector();
        if (last) {
            return lastIndexOf(typedVector, length, typedVectorValue, lengthValue, byteOffset);
        } else {
            return indexOf(typedVector, length, typedVectorValue, lengthValue, byteOffset);
        }
    } else if (value.isNumber()) {
        uint8_t byteValue = static_cast<uint8_t>((value.toInt32(lexicalGlobalObject)) % 256);
        RETURN_IF_EXCEPTION(scope, -1);

        if (last) {
            for (int64_t i = byteOffset; i >= 0; --i) {
                if (byteValue == typedVector[i]) {
                    return i;
                }
            }
        } else {
            const void* offset = memchr(reinterpret_cast<const void*>(typedVector + byteOffset), byteValue, length - byteOffset);
            if (offset != NULL) {
                return static_cast<int64_t>(static_cast<const uint8_t*>(offset) - typedVector);
            }
        }

        return -1;
    } else if (auto* arrayValue = JSC::jsDynamicCast<JSC::JSUint8Array*>(value)) {
        size_t lengthValue = arrayValue->byteLength();
        const uint8_t* typedVectorValue = arrayValue->typedVector();
        if (last) {
            return lastIndexOf(typedVector, length, typedVectorValue, lengthValue, byteOffset);
        } else {
            return indexOf(typedVector, length, typedVectorValue, lengthValue, byteOffset);
        }
    } else {
        throwTypeError(lexicalGlobalObject, scope, "Invalid value type"_s);
        return -1;
    }

    return -1;
}

static inline JSC::EncodedJSValue jsBufferPrototypeFunction_includesBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSArrayBufferView>::ClassParameter castedThis)
{
    auto index = indexOf(lexicalGlobalObject, callFrame, castedThis, false);
    return JSC::JSValue::encode(jsBoolean(index != -1));
}
static inline JSC::EncodedJSValue jsBufferPrototypeFunction_indexOfBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSArrayBufferView>::ClassParameter castedThis)
{
    auto index = indexOf(lexicalGlobalObject, callFrame, castedThis, false);
    return JSC::JSValue::encode(jsNumber(index));
}
static inline JSC::EncodedJSValue jsBufferPrototypeFunction_lastIndexOfBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSArrayBufferView>::ClassParameter castedThis)
{
    auto index = indexOf(lexicalGlobalObject, callFrame, castedThis, true);
    return JSC::JSValue::encode(jsNumber(index));
}
static inline JSC::EncodedJSValue jsBufferPrototypeFunction_swap16Body(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSArrayBufferView>::ClassParameter castedThis)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    auto scope = DECLARE_THROW_SCOPE(vm);

    constexpr int elemSize = 2;
    int64_t length = static_cast<int64_t>(castedThis->byteLength());
    if (length % elemSize != 0) {
        throwRangeError(lexicalGlobalObject, scope, "Buffer size must be a multiple of 16-bits"_s);
        return JSC::JSValue::encode(jsUndefined());
    }

    if (UNLIKELY(castedThis->isDetached())) {
        throwVMTypeError(lexicalGlobalObject, scope, "Buffer is detached"_s);
        return JSValue::encode(jsUndefined());
    }

    uint8_t* typedVector = castedThis->typedVector();

    for (size_t elem = 0; elem < length; elem += elemSize) {
        const size_t right = elem + 1;

        uint8_t temp = typedVector[elem];
        typedVector[elem] = typedVector[right];
        typedVector[right] = temp;
    }

    return JSC::JSValue::encode(castedThis);
}
static inline JSC::EncodedJSValue jsBufferPrototypeFunction_swap32Body(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSArrayBufferView>::ClassParameter castedThis)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    auto scope = DECLARE_THROW_SCOPE(vm);

    constexpr int elemSize = 4;
    int64_t length = static_cast<int64_t>(castedThis->byteLength());
    if (length % elemSize != 0) {
        throwRangeError(lexicalGlobalObject, scope, "Buffer size must be a multiple of 32-bits"_s);
        return JSC::JSValue::encode(jsUndefined());
    }

    if (UNLIKELY(castedThis->isDetached())) {
        throwVMTypeError(lexicalGlobalObject, scope, "Buffer is detached"_s);
        return JSValue::encode(jsUndefined());
    }

    uint8_t* typedVector = castedThis->typedVector();

    constexpr size_t swaps = elemSize / 2;
    for (size_t elem = 0; elem < length; elem += elemSize) {
        const size_t right = elem + elemSize - 1;
        for (size_t k = 0; k < swaps; k++) {
            const size_t i = right - k;
            const size_t j = elem + k;

            uint8_t temp = typedVector[i];
            typedVector[i] = typedVector[j];
            typedVector[j] = temp;
        }
    }

    return JSC::JSValue::encode(castedThis);
}
static inline JSC::EncodedJSValue jsBufferPrototypeFunction_swap64Body(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSArrayBufferView>::ClassParameter castedThis)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    auto scope = DECLARE_THROW_SCOPE(vm);

    constexpr size_t elemSize = 8;
    int64_t length = static_cast<int64_t>(castedThis->byteLength());
    if (length % elemSize != 0) {
        throwRangeError(lexicalGlobalObject, scope, "Buffer size must be a multiple of 64-bits"_s);
        return JSC::JSValue::encode(jsUndefined());
    }

    if (UNLIKELY(castedThis->isDetached())) {
        throwVMTypeError(lexicalGlobalObject, scope, "Buffer is detached"_s);
        return JSValue::encode(jsUndefined());
    }

    uint8_t* typedVector = castedThis->typedVector();

    constexpr size_t swaps = elemSize / 2;
    for (size_t elem = 0; elem < length; elem += elemSize) {
        const size_t right = elem + elemSize - 1;
        for (size_t k = 0; k < swaps; k++) {
            const size_t i = right - k;
            const size_t j = elem + k;

            uint8_t temp = typedVector[i];
            typedVector[i] = typedVector[j];
            typedVector[j] = temp;
        }
    }

    return JSC::JSValue::encode(castedThis);
}

static inline JSC::EncodedJSValue jsBufferToString(JSC::VM& vm, JSC::JSGlobalObject* lexicalGlobalObject, JSC::JSUint8Array* castedThis, size_t offset, size_t length, WebCore::BufferEncodingType encoding)
{
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (UNLIKELY(length == 0)) {
        RELEASE_AND_RETURN(scope, JSC::JSValue::encode(JSC::jsEmptyString(vm)));
    }

    JSC::EncodedJSValue ret = 0;

    switch (encoding) {
    case WebCore::BufferEncodingType::latin1: {
        LChar* data = nullptr;
        auto str = String::createUninitialized(length, data);
        memcpy(data, reinterpret_cast<const char*>(castedThis->typedVector() + offset), length);
        return JSC::JSValue::encode(JSC::jsString(vm, WTFMove(str)));
    }

    case WebCore::BufferEncodingType::ucs2:
    case WebCore::BufferEncodingType::utf16le: {
        UChar* data = nullptr;
        size_t u16length = length / 2;
        if (u16length == 0) {
            return JSC::JSValue::encode(JSC::jsEmptyString(vm));
        } else {
            auto str = String::createUninitialized(u16length, data);
            // always zero out the last byte of the string incase the buffer is not a multiple of 2
            data[u16length - 1] = 0;
            memcpy(data, reinterpret_cast<const char*>(castedThis->typedVector() + offset), length);
            return JSC::JSValue::encode(JSC::jsString(vm, WTFMove(str)));
        }

        break;
    }

    case WebCore::BufferEncodingType::ascii: {
        // ascii: we always know the length
        // so we might as well allocate upfront
        LChar* data = nullptr;
        auto str = String::createUninitialized(length, data);
        Bun__encoding__writeLatin1(castedThis->typedVector() + offset, length, data, length, static_cast<uint8_t>(encoding));
        return JSC::JSValue::encode(JSC::jsString(vm, WTFMove(str)));
    }

    case WebCore::BufferEncodingType::buffer:
    case WebCore::BufferEncodingType::utf8:
    case WebCore::BufferEncodingType::base64:
    case WebCore::BufferEncodingType::base64url:
    case WebCore::BufferEncodingType::hex: {
        ret = Bun__encoding__toString(castedThis->typedVector() + offset, length, lexicalGlobalObject, static_cast<uint8_t>(encoding));
        break;
    }
    default: {
        throwTypeError(lexicalGlobalObject, scope, "Unsupported encoding? This shouldn't happen"_s);
        break;
    }
    }

    JSC::JSValue retValue = JSC::JSValue::decode(ret);
    if (UNLIKELY(!retValue.isString())) {
        scope.throwException(lexicalGlobalObject, retValue);
        return JSC::JSValue::encode(jsUndefined());
    }

    RELEASE_AND_RETURN(scope, JSC::JSValue::encode(retValue));
}

static inline JSC::EncodedJSValue jsBufferPrototypeFunction_toStringBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSArrayBufferView>::ClassParameter castedThis)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    auto scope = DECLARE_THROW_SCOPE(vm);
    uint32_t offset = 0;
    uint32_t length = castedThis->length();
    uint32_t byteLength = length;
    WebCore::BufferEncodingType encoding = WebCore::BufferEncodingType::utf8;

    if (length == 0)
        return JSC::JSValue::encode(JSC::jsEmptyString(vm));

    size_t argsCount = callFrame->argumentCount();

    JSC::JSValue arg1 = callFrame->argument(0);
    JSC::JSValue arg2 = callFrame->argument(1);
    JSC::JSValue arg3 = callFrame->argument(2);

    // This method could be called in following forms:
    // - toString()
    // - toString(encoding)
    // - toString(encoding, start)
    // - toString(encoding, start, end)
    // - toString(offset, length)
    // - toString(offset, length, encoding)
    if (argsCount == 0)
        return jsBufferToString(vm, lexicalGlobalObject, castedThis, offset, length, encoding);

    if (arg1.isString()) {
        encoding = parseEncoding(lexicalGlobalObject, scope, arg1);
        RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));

        if (!arg3.isUndefined()) {
            // length is end
            length = std::min(byteLength, static_cast<uint32_t>(arg3.toInt32(lexicalGlobalObject)));
            RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));
        }

        int32_t istart = 0;

        if (!arg2.isUndefined()) {
            istart = arg2.toInt32(lexicalGlobalObject);
            RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));
        }

        if (istart < 0) {
            throwTypeError(lexicalGlobalObject, scope, "Start must be a positive integer"_s);
            return JSC::JSValue::encode(jsUndefined());
        }
        offset = static_cast<uint32_t>(istart);
        length = (length > offset) ? (length - offset) : 0;
    } else {

        int32_t ioffset = 0;

        if (!arg1.isUndefined()) {
            ioffset = arg1.toInt32(lexicalGlobalObject);
            RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));
        }

        if (ioffset < 0) {
            throwTypeError(lexicalGlobalObject, scope, "Offset must be a positive integer"_s);
            return JSC::JSValue::encode(jsUndefined());
        }

        offset = static_cast<uint32_t>(ioffset);
        length = (length > offset) ? (length - offset) : 0;

        if (!arg3.isUndefined()) {
            encoding = parseEncoding(lexicalGlobalObject, scope, arg3);
            RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));
        }

        if (!arg2.isUndefined())
            length = std::min(length, static_cast<uint32_t>(arg2.toInt32(lexicalGlobalObject)));
    }

    return jsBufferToString(vm, lexicalGlobalObject, castedThis, offset, length, encoding);
}

// DOMJIT makes it slower! TODO: investigate why
// JSC_DECLARE_JIT_OPERATION_WITHOUT_WTF_INTERNAL(jsBufferPrototypeToStringWithoutTypeChecks, JSValue, (JSC::JSGlobalObject * lexicalGlobalObject, JSC::JSUint8Array* thisValue, JSC::JSString* encodingValue));

// JSC_DEFINE_JIT_OPERATION(jsBufferPrototypeToStringWithoutTypeChecks, JSValue, (JSC::JSGlobalObject * lexicalGlobalObject, JSUint8Array* thisValue, JSString* encodingValue))
// {
//     VM& vm = JSC::getVM(lexicalGlobalObject);
//     IGNORE_WARNINGS_BEGIN("frame-address")
//     CallFrame* callFrame = DECLARE_CALL_FRAME(vm);
//     IGNORE_WARNINGS_END
//     JSC::JITOperationPrologueCallFrameTracer tracer(vm, callFrame);

//     std::optional<BufferEncodingType> encoded = parseEnumeration<BufferEncodingType>(*lexicalGlobalObject, encodingValue);
//     if (!encoded) {
//         auto scope = DECLARE_THROW_SCOPE(vm);

//         throwTypeError(lexicalGlobalObject, scope, "Invalid encoding"_s);
//         return {};
//     }

//     auto encoding = encoded.value();

//     return JSValue::decode(jsBufferToString(vm, lexicalGlobalObject, thisValue, 0, thisValue->byteLength(), encoding));
// }

static inline JSC::EncodedJSValue jsBufferPrototypeFunction_writeBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSArrayBufferView>::ClassParameter castedThis)
{
    auto& vm = JSC::getVM(lexicalGlobalObject);
    uint32_t offset = 0;
    uint32_t length = castedThis->byteLength();
    uint32_t max = length;
    WebCore::BufferEncodingType encoding = WebCore::BufferEncodingType::utf8;

    auto scope = DECLARE_THROW_SCOPE(vm);

    if (UNLIKELY(callFrame->argumentCount() == 0)) {
        throwTypeError(lexicalGlobalObject, scope, "Not enough arguments"_s);
        return JSC::JSValue::encode(jsUndefined());
    }

    EnsureStillAliveScope arg0 = callFrame->argument(0);
    auto* str = arg0.value().toStringOrNull(lexicalGlobalObject);
    if (!str) {
        throwTypeError(lexicalGlobalObject, scope, "write() expects a string"_s);
        return JSC::JSValue::encode(jsUndefined());
    }

    JSValue offsetValue = jsUndefined();
    JSValue lengthValue = jsUndefined();
    JSValue encodingValue = jsUndefined();

    switch (callFrame->argumentCount()) {
    case 4:
        encodingValue = callFrame->uncheckedArgument(3);
        FALLTHROUGH;
    case 3:
        lengthValue = callFrame->uncheckedArgument(2);
        FALLTHROUGH;
    case 2:
        offsetValue = callFrame->uncheckedArgument(1);
        break;
    default:
        break;
    }

    auto setEncoding = [&]() {
        if (!encodingValue.isUndefined()) {
            encoding = parseEncoding(lexicalGlobalObject, scope, encodingValue);
        }
    };

    if (offsetValue.isUndefined()) {
        // https://github.com/nodejs/node/blob/e676942f814915b2d24fc899bb42dc71ae6c8226/lib/buffer.js#L1053
        RELEASE_AND_RETURN(scope, writeToBuffer(lexicalGlobalObject, castedThis, str, offset, length, encoding));
    }

    if (lengthValue.isUndefined() && offsetValue.isString()) {
        // https://github.com/nodejs/node/blob/e676942f814915b2d24fc899bb42dc71ae6c8226/lib/buffer.js#L1056
        encodingValue = offsetValue;
        setEncoding();
        RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));
        RELEASE_AND_RETURN(scope, writeToBuffer(lexicalGlobalObject, castedThis, str, offset, length, encoding));
    }

    if (UNLIKELY(!offsetValue.isNumber())) {
        throwTypeError(lexicalGlobalObject, scope, "Invalid offset"_s);
        return JSC::JSValue::encode(jsUndefined());
    }

    int32_t userOffset = offsetValue.toInt32(lexicalGlobalObject);
    RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));
    if (userOffset < 0 || userOffset > max) {
        throwRangeError(lexicalGlobalObject, scope, "Offset is out of bounds"_s);
        return JSC::JSValue::encode(jsUndefined());
    }
    offset = static_cast<uint32_t>(userOffset);
    uint32_t remaining = max - static_cast<uint32_t>(userOffset);

    // https://github.com/nodejs/node/blob/e676942f814915b2d24fc899bb42dc71ae6c8226/lib/buffer.js#L1062-L1077
    if (lengthValue.isUndefined()) {
        length = remaining;
    } else if (lengthValue.isString()) {
        encodingValue = lengthValue;
        setEncoding();
        RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));
        length = remaining;
    } else {
        setEncoding();

        int32_t userLength = lengthValue.toInt32(lexicalGlobalObject);
        RETURN_IF_EXCEPTION(scope, JSC::JSValue::encode(jsUndefined()));
        length = std::min(static_cast<uint32_t>(userLength), remaining);
    }

    RELEASE_AND_RETURN(scope, writeToBuffer(lexicalGlobalObject, castedThis, str, offset, length, encoding));
}

JSC_DEFINE_HOST_FUNCTION(jsBufferConstructorFunction_alloc, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return jsBufferConstructorFunction_allocBody(lexicalGlobalObject, callFrame);
}
JSC_DEFINE_HOST_FUNCTION(jsBufferConstructorFunction_allocUnsafe, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return jsBufferConstructorFunction_allocUnsafeBody(lexicalGlobalObject, callFrame);
}
JSC_DEFINE_HOST_FUNCTION(jsBufferConstructorFunction_allocUnsafeSlow, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return jsBufferConstructorFunction_allocUnsafeSlowBody(lexicalGlobalObject, callFrame);
}
JSC_DEFINE_HOST_FUNCTION(jsBufferConstructorFunction_byteLength, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return jsBufferConstructorFunction_byteLengthBody(lexicalGlobalObject, callFrame);
}

JSC_DEFINE_HOST_FUNCTION(jsBufferConstructorFunction_toBuffer, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return jsBufferConstructorFunction_toBufferBody(lexicalGlobalObject, callFrame);
}

class JSBufferConstructor final : public JSC::InternalFunction {
public:
    using Base = JSC::InternalFunction;
    static constexpr unsigned StructureFlags = Base::StructureFlags;

    ~JSBufferConstructor() = default;

    static void destroy(JSC::JSCell* cell)
    {
        static_cast<JSBufferConstructor*>(cell)->JSBufferConstructor::~JSBufferConstructor();
    }

    static JSC::Structure* createStructure(JSC::VM& vm, JSC::JSGlobalObject* globalObject)
    {
        JSValue prototype = globalObject->m_typedArrayUint8.constructorInitializedOnMainThread(globalObject);
        return JSC::Structure::create(vm, globalObject, prototype, JSC::TypeInfo(prototype.asCell()->type(), StructureFlags), info());
    }

    DECLARE_INFO;

    static JSBufferConstructor* create(JSC::VM& vm, JSC::JSGlobalObject* globalObject, JSC::Structure* structure, JSC::JSObject* prototype)
    {
        JSBufferConstructor* constructor = new (NotNull, JSC::allocateCell<JSBufferConstructor>(vm)) JSBufferConstructor(vm, globalObject, structure);
        constructor->finishCreation(vm, globalObject, prototype);
        return constructor;
    }

private:
    JSBufferConstructor(JSC::VM& vm, JSGlobalObject* globalObject, JSC::Structure* structure)
        : Base(vm, structure, constructJSBuffer, constructJSBuffer)

    {
    }

    void finishCreation(JSC::VM&, JSGlobalObject*, JSC::JSObject* prototype);

}

JSC_DEFINE_HOST_FUNCTION(jsBufferConstructorFunction_isEncoding, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return jsBufferConstructorFunction_isEncodingBody(lexicalGlobalObject, callFrame);
}

JSC_DEFINE_HOST_FUNCTION(jsBufferConstructorFunction_compare, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return jsBufferConstructorFunction_compareBody(lexicalGlobalObject, callFrame);
}

JSC_DEFINE_HOST_FUNCTION(jsBufferConstructorFunction_concat, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return jsBufferConstructorFunction_concatBody(lexicalGlobalObject, callFrame);
}

extern "C" JSC_DECLARE_JIT_OPERATION_WITHOUT_WTF_INTERNAL(jsBufferConstructorAllocWithoutTypeChecks, JSUint8Array*, (JSC::JSGlobalObject * lexicalGlobalObject, void* thisValue, int size));
extern "C" JSC_DECLARE_JIT_OPERATION_WITHOUT_WTF_INTERNAL(jsBufferConstructorAllocUnsafeWithoutTypeChecks, JSUint8Array*, (JSC::JSGlobalObject * lexicalGlobalObject, void* thisValue, int size));
extern "C" JSC_DECLARE_JIT_OPERATION_WITHOUT_WTF_INTERNAL(jsBufferConstructorAllocUnsafeSlowWithoutTypeChecks, JSUint8Array*, (JSC::JSGlobalObject * lexicalGlobalObject, void* thisValue, int size));

JSC_DEFINE_JIT_OPERATION(jsBufferConstructorAllocWithoutTypeChecks, JSUint8Array*, (JSC::JSGlobalObject * lexicalGlobalObject, void* thisValue, int byteLength))
{
    VM& vm = JSC::getVM(lexicalGlobalObject);
    IGNORE_WARNINGS_BEGIN("frame-address")
    CallFrame* callFrame = DECLARE_CALL_FRAME(vm);
    IGNORE_WARNINGS_END
    JSC::JITOperationPrologueCallFrameTracer tracer(vm, callFrame);
    return allocBuffer(lexicalGlobalObject, byteLength);
}

JSC_DEFINE_JIT_OPERATION(jsBufferConstructorAllocUnsafeWithoutTypeChecks, JSUint8Array*, (JSC::JSGlobalObject * lexicalGlobalObject, void* thisValue, int byteLength))
{
    VM& vm = JSC::getVM(lexicalGlobalObject);
    IGNORE_WARNINGS_BEGIN("frame-address")
    CallFrame* callFrame = DECLARE_CALL_FRAME(vm);
    IGNORE_WARNINGS_END
    JSC::JITOperationPrologueCallFrameTracer tracer(vm, callFrame);
    return allocBufferUnsafe(lexicalGlobalObject, byteLength);
}

JSC_DEFINE_JIT_OPERATION(jsBufferConstructorAllocUnsafeSlowWithoutTypeChecks, JSUint8Array*, (JSC::JSGlobalObject * lexicalGlobalObject, void* thisValue, int byteLength))
{
    VM& vm = JSC::getVM(lexicalGlobalObject);
    IGNORE_WARNINGS_BEGIN("frame-address")
    CallFrame* callFrame = DECLARE_CALL_FRAME(vm);
    IGNORE_WARNINGS_END
    JSC::JITOperationPrologueCallFrameTracer tracer(vm, callFrame);
    return allocBufferUnsafe(lexicalGlobalObject, byteLength);
}

JSC_ANNOTATE_HOST_FUNCTION(JSBufferConstructorConstruct, JSBufferConstructor::construct);

const ClassInfo JSBufferConstructor::s_info = { "Buffer"_s, nullptr, nullptr, nullptr, CREATE_METHOD_TABLE(JSBufferConstructor) };

class JSBuffer : public JSC::JSNonFinalObject {

    DECLARE_INFO;

    static constexpr JSC::JSTypeRange typeRange = { Uint8ArrayType, Uint8ArrayType };
};

const ClassInfo JSBuffer::s_info = {
    "Buffer"_s,
    JSC::getUint8ArrayClassInfo(),
    nullptr,
    nullptr,
    CREATE_METHOD_TABLE(JSBuffer)
};

JSC_DEFINE_HOST_FUNCTION(jsBufferPrototypeFunction_compare, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSArrayBufferView>::call<jsBufferPrototypeFunction_compareBody>(*lexicalGlobalObject, *callFrame, "compare");
}
JSC_DEFINE_HOST_FUNCTION(jsBufferPrototypeFunction_copy, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSArrayBufferView>::call<jsBufferPrototypeFunction_copyBody>(*lexicalGlobalObject, *callFrame, "copy");
}
JSC_DEFINE_HOST_FUNCTION(jsBufferPrototypeFunction_equals, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSArrayBufferView>::call<jsBufferPrototypeFunction_equalsBody>(*lexicalGlobalObject, *callFrame, "equals");
}
JSC_DEFINE_HOST_FUNCTION(jsBufferPrototypeFunction_fill, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSArrayBufferView>::call<jsBufferPrototypeFunction_fillBody>(*lexicalGlobalObject, *callFrame, "fill");
}
JSC_DEFINE_HOST_FUNCTION(jsBufferPrototypeFunction_includes, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSArrayBufferView>::call<jsBufferPrototypeFunction_includesBody>(*lexicalGlobalObject, *callFrame, "includes");
}
JSC_DEFINE_HOST_FUNCTION(jsBufferPrototypeFunction_indexOf, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSArrayBufferView>::call<jsBufferPrototypeFunction_indexOfBody>(*lexicalGlobalObject, *callFrame, "indexOf");
}
JSC_DEFINE_HOST_FUNCTION(jsBufferPrototypeFunction_lastIndexOf, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSArrayBufferView>::call<jsBufferPrototypeFunction_lastIndexOfBody>(*lexicalGlobalObject, *callFrame, "lastIndexOf");
}
JSC_DEFINE_HOST_FUNCTION(jsBufferPrototypeFunction_swap16, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSArrayBufferView>::call<jsBufferPrototypeFunction_swap16Body>(*lexicalGlobalObject, *callFrame, "swap16");
}
JSC_DEFINE_HOST_FUNCTION(jsBufferPrototypeFunction_swap32, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSArrayBufferView>::call<jsBufferPrototypeFunction_swap32Body>(*lexicalGlobalObject, *callFrame, "swap32");
}
JSC_DEFINE_HOST_FUNCTION(jsBufferPrototypeFunction_swap64, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSArrayBufferView>::call<jsBufferPrototypeFunction_swap64Body>(*lexicalGlobalObject, *callFrame, "swap64");
}
JSC_DEFINE_HOST_FUNCTION(jsBufferPrototypeFunction_toString, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSArrayBufferView>::call<jsBufferPrototypeFunction_toStringBody>(*lexicalGlobalObject, *callFrame, "toString");
}
JSC_DEFINE_HOST_FUNCTION(jsBufferPrototypeFunction_write, (JSGlobalObject * lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSArrayBufferView>::call<jsBufferPrototypeFunction_writeBody>(*lexicalGlobalObject, *callFrame, "write");
}

/* */

/* Hash table for prototype */

static const HashTableValue JSBufferPrototypeTableValues[]
    = {
          { "asciiSlice"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeAsciiSliceCodeGenerator, 2 } },
          { "asciiWrite"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeAsciiWriteCodeGenerator, 1 } },
          { "base64Slice"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeBase64SliceCodeGenerator, 2 } },
          { "base64Write"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeBase64WriteCodeGenerator, 1 } },
          { "base64urlSlice"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeBase64urlSliceCodeGenerator, 2 } },
          { "base64urlWrite"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeBase64urlWriteCodeGenerator, 1 } },
          { "compare"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferPrototypeFunction_compare, 5 } },
          { "copy"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferPrototypeFunction_copy, 4 } },
          { "equals"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferPrototypeFunction_equals, 1 } },
          { "fill"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferPrototypeFunction_fill, 4 } },
          { "hexSlice"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeHexSliceCodeGenerator, 2 } },
          { "hexWrite"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeHexWriteCodeGenerator, 1 } },
          { "includes"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferPrototypeFunction_includes, 3 } },
          { "indexOf"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferPrototypeFunction_indexOf, 3 } },
          { "inspect"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeInspectCodeGenerator, 2 } },
          { "lastIndexOf"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferPrototypeFunction_lastIndexOf, 3 } },
          { "latin1Slice"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeLatin1SliceCodeGenerator, 2 } },
          { "latin1Write"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeLatin1WriteCodeGenerator, 1 } },
          { "offset"_s, static_cast<unsigned>(JSC::PropertyAttribute::DontEnum | JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::Accessor | JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeOffsetCodeGenerator, 0 } },
          { "parent"_s, static_cast<unsigned>(JSC::PropertyAttribute::DontEnum | JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::Accessor | JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeParentCodeGenerator, 0 } },
          { "readBigInt64"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadBigInt64LECodeGenerator, 1 } },
          { "readBigInt64BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadBigInt64BECodeGenerator, 1 } },
          { "readBigInt64LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadBigInt64LECodeGenerator, 1 } },
          { "readBigUInt64"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadBigUInt64LECodeGenerator, 1 } },
          { "readBigUInt64BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadBigUInt64BECodeGenerator, 1 } },
          { "readBigUInt64LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadBigUInt64LECodeGenerator, 1 } },
          { "readDouble"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadDoubleLECodeGenerator, 1 } },
          { "readDoubleBE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadDoubleBECodeGenerator, 1 } },
          { "readDoubleLE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadDoubleLECodeGenerator, 1 } },
          { "readFloat"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadFloatLECodeGenerator, 1 } },
          { "readFloatBE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadFloatBECodeGenerator, 1 } },
          { "readFloatLE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadFloatLECodeGenerator, 1 } },
          { "readInt16"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadInt16LECodeGenerator, 1 } },
          { "readInt16BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadInt16BECodeGenerator, 1 } },
          { "readInt16LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadInt16LECodeGenerator, 1 } },
          { "readInt32"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadInt32LECodeGenerator, 1 } },
          { "readInt32BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadInt32BECodeGenerator, 1 } },
          { "readInt32LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadInt32LECodeGenerator, 1 } },
          { "readInt8"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadInt8CodeGenerator, 2 } },
          { "readIntBE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadIntBECodeGenerator, 1 } },
          { "readIntLE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadIntLECodeGenerator, 1 } },
          { "readUInt16BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUInt16BECodeGenerator, 1 } },
          { "readUInt16LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUInt16LECodeGenerator, 1 } },
          { "readUInt32BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUInt32BECodeGenerator, 1 } },
          { "readUInt32LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUInt32LECodeGenerator, 1 } },
          { "readUInt8"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUInt8CodeGenerator, 1 } },
          { "readUIntBE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUIntBECodeGenerator, 1 } },
          { "readUIntLE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUIntLECodeGenerator, 1 } },
          // name alias
          { "readUintBE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUIntBECodeGenerator, 1 } },
          { "readUintLE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUIntLECodeGenerator, 1 } },
          { "readUint8"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUInt8CodeGenerator, 1 } },
          { "readUint16BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUInt16BECodeGenerator, 1 } },
          { "readUint16LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUInt16LECodeGenerator, 1 } },
          { "readUint32BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUInt32BECodeGenerator, 1 } },
          { "readUint32LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadUInt32LECodeGenerator, 1 } },
          { "readBigUint64BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadBigUInt64BECodeGenerator, 1 } },
          { "readBigUint64LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeReadBigUInt64LECodeGenerator, 1 } },

          { "slice"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeSliceCodeGenerator, 2 } },
          { "subarray"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeSliceCodeGenerator, 2 } },
          { "swap16"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferPrototypeFunction_swap16, 0 } },
          { "swap32"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferPrototypeFunction_swap32, 0 } },
          { "swap64"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferPrototypeFunction_swap64, 0 } },
          { "toJSON"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeToJSONCodeGenerator, 1 } },
          { "toLocaleString"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferPrototypeFunction_toString, 4 } },
          { "toString"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferPrototypeFunction_toString, 4 } },
          { "ucs2Slice"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeUcs2SliceCodeGenerator, 2 } },
          { "ucs2Write"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeUcs2WriteCodeGenerator, 1 } },
          { "utf16leSlice"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeUtf16leSliceCodeGenerator, 2 } },
          { "utf16leWrite"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeUtf16leWriteCodeGenerator, 1 } },
          { "utf8Slice"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeUtf8SliceCodeGenerator, 2 } },
          { "utf8Write"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeUtf8WriteCodeGenerator, 1 } },
          { "write"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferPrototypeFunction_write, 4 } },
          { "writeBigInt64BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteBigInt64BECodeGenerator, 1 } },
          { "writeBigInt64LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteBigInt64LECodeGenerator, 1 } },
          { "writeBigUInt64BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteBigUInt64BECodeGenerator, 1 } },
          { "writeBigUInt64LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteBigUInt64LECodeGenerator, 1 } },
          { "writeDouble"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteDoubleLECodeGenerator, 1 } },
          { "writeDoubleBE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteDoubleBECodeGenerator, 1 } },
          { "writeDoubleLE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteDoubleLECodeGenerator, 1 } },
          { "writeFloat"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteFloatLECodeGenerator, 1 } },
          { "writeFloatBE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteFloatBECodeGenerator, 1 } },
          { "writeFloatLE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteFloatLECodeGenerator, 1 } },
          { "writeInt16BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteInt16BECodeGenerator, 1 } },
          { "writeInt16LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteInt16LECodeGenerator, 1 } },
          { "writeInt32BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteInt32BECodeGenerator, 1 } },
          { "writeInt32LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteInt32LECodeGenerator, 1 } },
          { "writeInt8"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteInt8CodeGenerator, 1 } },
          { "writeIntBE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteIntBECodeGenerator, 1 } },
          { "writeIntLE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteIntLECodeGenerator, 1 } },
          { "writeUInt16"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt16LECodeGenerator, 1 } },
          { "writeUInt16BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt16BECodeGenerator, 1 } },
          { "writeUInt16LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt16LECodeGenerator, 1 } },
          { "writeUInt32"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt32LECodeGenerator, 1 } },
          { "writeUInt32BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt32BECodeGenerator, 1 } },
          { "writeUInt32LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt32LECodeGenerator, 1 } },
          { "writeUInt8"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt8CodeGenerator, 1 } },
          { "writeUIntBE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUIntBECodeGenerator, 1 } },
          { "writeUIntLE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUIntLECodeGenerator, 1 } },
          // name alias
          { "writeUintBE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUIntBECodeGenerator, 1 } },
          { "writeUintLE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUIntLECodeGenerator, 1 } },
          { "writeUint8"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt8CodeGenerator, 1 } },
          { "writeUint16"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt16LECodeGenerator, 1 } },
          { "writeUint16BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt16BECodeGenerator, 1 } },
          { "writeUint16LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt16LECodeGenerator, 1 } },
          { "writeUint32"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt32LECodeGenerator, 1 } },
          { "writeUint32BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt32BECodeGenerator, 1 } },
          { "writeUint32LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteUInt32LECodeGenerator, 1 } },
          { "writeBigUint64BE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteBigUInt64BECodeGenerator, 1 } },
          { "writeBigUint64LE"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferPrototypeWriteBigUInt64LECodeGenerator, 1 } },
      };

void JSBufferPrototype::finishCreation(VM& vm, JSC::JSGlobalObject* globalThis)
{
    Base::finishCreation(vm);
    JSC_TO_STRING_TAG_WITHOUT_TRANSITION();
    auto clientData = WebCore::clientData(vm);
    reifyStaticProperties(vm, JSBuffer::info(), JSBufferPrototypeTableValues, *this);
}

const ClassInfo JSBufferPrototype::s_info = {
    // In Node.js, Object.prototype.toString.call(new Buffer(0)) returns "[object Uint8Array]".
    // We must use the same naming convention to match Node
    // Some packages (like MongoDB's official Node.js client) rely on this behavior.
    "Uint8Array"_s,

    nullptr, nullptr, nullptr, CREATE_METHOD_TABLE(JSBufferPrototype)
};

static const JSC::DOMJIT::Signature DOMJITSignaturejsBufferConstructorAlloc(jsBufferConstructorAllocWithoutTypeChecks,
    JSBufferConstructor::info(),
    JSC::DOMJIT::Effect::forWriteKinds(JSC::DFG::AbstractHeapKind::Heap),
    JSC::SpecUint8Array, JSC::SpecInt32Only);

static const JSC::DOMJIT::Signature DOMJITSignaturejsBufferConstructorAllocUnsafe(jsBufferConstructorAllocUnsafeWithoutTypeChecks,
    JSBufferConstructor::info(),
    JSC::DOMJIT::Effect::forWriteKinds(JSC::DFG::AbstractHeapKind::Heap),
    JSC::SpecUint8Array, JSC::SpecInt32Only);
static const JSC::DOMJIT::Signature DOMJITSignaturejsBufferConstructorAllocUnsafeSlow(jsBufferConstructorAllocUnsafeSlowWithoutTypeChecks,
    JSBufferConstructor::info(),
    JSC::DOMJIT::Effect::forWriteKinds(JSC::DFG::AbstractHeapKind::Heap),
    JSC::SpecUint8Array, JSC::SpecInt32Only);

/* Hash table for constructor */
static const HashTableValue JSBufferConstructorTableValues[] = {
    { "alloc"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function | JSC::PropertyAttribute::DOMJITFunction), NoIntrinsic, { HashTableValue::DOMJITFunctionType, jsBufferConstructorFunction_alloc, &DOMJITSignaturejsBufferConstructorAlloc } },
    // { "alloc"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function | JSC::PropertyAttribute::DOMJITFunction), NoIntrinsic, { HashTableValue::DOMJITFunctionType, jsBufferConstructorFunction_alloc, &DOMJITSignaturejsBufferConstructorAlloc } },
    // { "allocUnsafe"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferConstructorFunction_allocUnsafe, 1 } },
    { "allocUnsafe"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function | JSC::PropertyAttribute::DOMJITFunction), NoIntrinsic, { HashTableValue::DOMJITFunctionType, jsBufferConstructorFunction_allocUnsafe, &DOMJITSignaturejsBufferConstructorAllocUnsafe } },
    // { "allocUnsafeSlow"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferConstructorFunction_allocUnsafeSlow, 1 } },
    { "allocUnsafeSlow"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function | JSC::PropertyAttribute::DOMJITFunction), NoIntrinsic, { HashTableValue::DOMJITFunctionType, jsBufferConstructorFunction_allocUnsafeSlow, &DOMJITSignaturejsBufferConstructorAllocUnsafeSlow } },
    { "byteLength"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferConstructorFunction_byteLength, 2 } },
    { "compare"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferConstructorFunction_compare, 2 } },
    { "concat"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferConstructorFunction_concat, 2 } },
    { "from"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferConstructorFromCodeGenerator, 1 } },
    { "isBuffer"_s, static_cast<unsigned>(JSC::PropertyAttribute::Builtin), NoIntrinsic, { HashTableValue::BuiltinGeneratorType, jsBufferConstructorIsBufferCodeGenerator, 1 } },
    { "toBuffer"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferConstructorFunction_toBuffer, 1 } },
    { "isEncoding"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsBufferConstructorFunction_isEncoding, 1 } },
};

void JSBufferConstructor::finishCreation(VM& vm, JSGlobalObject* globalObject, JSC::JSObject* prototype)
{
    Base::finishCreation(vm, 3, "Buffer"_s, PropertyAdditionMode::WithoutStructureTransition);
    reifyStaticProperties(vm, JSBufferConstructor::info(), JSBufferConstructorTableValues, *this);
    putDirectWithoutTransition(vm, vm.propertyNames->prototype, prototype, PropertyAttribute::DontEnum | PropertyAttribute::DontDelete | PropertyAttribute::ReadOnly);
    prototype->putDirect(vm, vm.propertyNames->speciesSymbol, this, PropertyAttribute::DontDelete | PropertyAttribute::ReadOnly);
}

JSC::Structure* createBufferStructure(JSC::VM& vm, JSC::JSGlobalObject* globalObject, JSC::JSValue prototype)
{
    return JSBuffer::createStructure(vm, globalObject, prototype);
}

JSC::JSObject* createBufferPrototype(JSC::VM& vm, JSC::JSGlobalObject* globalObject)
{
    return JSBufferPrototype::create(vm, globalObject, JSBufferPrototype::createStructure(vm, globalObject, globalObject->m_typedArrayUint8.prototype(globalObject)));
}
JSC::JSObject* createBufferConstructor(JSC::VM& vm, JSC::JSGlobalObject* globalObject, JSC::JSObject* bufferPrototype)
{
    return JSBufferConstructor::create(
        vm,
        globalObject,
        JSBufferConstructor::createStructure(vm, globalObject),
        bufferPrototype);
}

} // namespace WebCore

// this is now a no-op
void toBuffer(JSC::JSGlobalObject* lexicalGlobalObject, JSC::JSUint8Array* uint8Array)
{
}

JSC_DEFINE_HOST_FUNCTION(constructJSBuffer, (JSC::JSGlobalObject * lexicalGlobalObject, JSC::CallFrame* callFrame))
{
    VM& vm = lexicalGlobalObject->vm();
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    size_t argsCount = callFrame->argumentCount();
    if (argsCount == 0) {
        RELEASE_AND_RETURN(throwScope, constructBufferEmpty(lexicalGlobalObject));
    }
    JSValue distinguishingArg = callFrame->uncheckedArgument(0);
    JSValue encodingArg = argsCount > 1 ? callFrame->uncheckedArgument(1) : JSValue();
    auto* globalObject = reinterpret_cast<Zig::GlobalObject*>(lexicalGlobalObject);

    if (distinguishingArg.isAnyInt()) {
        throwScope.release();
        return JSBuffer__bufferFromLength(lexicalGlobalObject, distinguishingArg.asAnyInt());
    } else if (distinguishingArg.isCell()) {
        auto type = distinguishingArg.asCell()->type();

        switch (type) {
        case StringType:
        case StringObjectType:
        case DerivedStringObjectType: {
            throwScope.release();
            return constructBufferFromStringAndEncoding(lexicalGlobalObject, distinguishingArg, encodingArg);
        }

        case Uint16ArrayType:
        case Uint32ArrayType:
        case Int8ArrayType:
        case Int16ArrayType:
        case Int32ArrayType:
        case Float32ArrayType:
        case Float64ArrayType:
        case BigInt64ArrayType:
        case BigUint64ArrayType: {
            // byteOffset and byteLength are ignored in this case, which is consitent with Node.js and new Uint8Array()
            JSC::JSArrayBufferView* view = jsCast<JSC::JSArrayBufferView*>(distinguishingArg.asCell());

            void* data = view->vector();
            size_t byteLength = view->length();

            if (UNLIKELY(!data)) {
                throwException(globalObject, throwScope, createRangeError(globalObject, "Buffer is detached"_s));
                return JSValue::encode({});
            }

            auto* subclassStructure = globalObject->JSBufferSubclassStructure();
            auto* uint8Array = JSC::JSUint8Array::createUninitialized(lexicalGlobalObject, subclassStructure, byteLength);
            if (UNLIKELY(!uint8Array)) {
                throwOutOfMemoryError(globalObject, throwScope);
                return JSValue::encode({});
            }

            if (byteLength) {
                uint8Array->setFromTypedArray(lexicalGlobalObject, 0, view, 0, byteLength, CopyType::LeftToRight);
            }

            RELEASE_AND_RETURN(throwScope, JSC::JSValue::encode(uint8Array));
            break;
        }

        case DataViewType:
        case Uint8ArrayType:
        case Uint8ClampedArrayType: {
            // byteOffset and byteLength are ignored in this case, which is consitent with Node.js and new Uint8Array()
            JSC::JSArrayBufferView* view = jsCast<JSC::JSArrayBufferView*>(distinguishingArg.asCell());

            void* data = view->vector();
            size_t byteLength = view->byteLength();

            if (UNLIKELY(!data)) {
                throwException(globalObject, throwScope, createRangeError(globalObject, "Buffer is detached"_s));
                return JSValue::encode({});
            }

            auto* subclassStructure = globalObject->JSBufferSubclassStructure();
            auto* uint8Array = JSC::JSUint8Array::createUninitialized(lexicalGlobalObject, subclassStructure, byteLength);
            if (UNLIKELY(!uint8Array)) {
                throwOutOfMemoryError(globalObject, throwScope);
                return JSValue::encode({});
            }

            if (byteLength) {
                memcpy(uint8Array->vector(), data, byteLength);
            }

            RELEASE_AND_RETURN(throwScope, JSC::JSValue::encode(uint8Array));
        }
        case ArrayBufferType: {
            // This closely matches `new Uint8Array(buffer, byteOffset, length)` in JavaScriptCore's implementation.
            // See Source/JavaScriptCore/runtime/JSGenericTypedArrayViewConstructorInlines.h
            size_t offset = 0;
            std::optional<size_t> length;
            if (argsCount > 1) {

                offset = callFrame->uncheckedArgument(1).toTypedArrayIndex(globalObject, "byteOffset"_s);

                // TOOD: return Node.js error
                RETURN_IF_EXCEPTION(throwScope, encodedJSValue());

                if (argsCount > 2) {
                    // If the length value is present but undefined, treat it as missing.
                    JSValue lengthValue = callFrame->uncheckedArgument(2);
                    if (!lengthValue.isUndefined()) {
                        length = lengthValue.toTypedArrayIndex(globalObject, "length"_s);

                        // TOOD: return Node.js error
                        RETURN_IF_EXCEPTION(throwScope, encodedJSValue());
                    }
                }
            }

            auto* jsBuffer = jsCast<JSC::JSArrayBuffer*>(distinguishingArg.asCell());
            RefPtr<ArrayBuffer> buffer = jsBuffer->impl();
            if (buffer->isDetached()) {
                // TOOD: return Node.js error
                throwTypeError(globalObject, throwScope, "Buffer is detached"_s);
                return JSValue::encode({});
            }

            if (!length) {
                size_t byteLength = buffer->byteLength();
                if (buffer->isResizableOrGrowableShared()) {
                    if (UNLIKELY(offset > byteLength)) {
                        // TOOD: return Node.js error
                        throwRangeError(globalObject, throwScope, "byteOffset exceeds source ArrayBuffer byteLength"_s);
                        return JSValue::encode({});
                    }
                } else {
                    length = (byteLength - offset);
                }
            }

            auto* subclassStructure = globalObject->JSBufferSubclassStructure();
            auto* uint8Array = JSC::JSUint8Array::create(lexicalGlobalObject, subclassStructure, WTFMove(buffer), offset, length);
            if (UNLIKELY(!uint8Array)) {
                throwOutOfMemoryError(globalObject, throwScope);
                return JSC::JSValue::encode({});
            }

            RELEASE_AND_RETURN(throwScope, JSC::JSValue::encode(uint8Array));
        }
        default: {
            break;
        }
        }
    }

    JSC::JSObject* constructor = lexicalGlobalObject->m_typedArrayUint8.constructor(lexicalGlobalObject);

    MarkedArgumentBuffer args;
    args.append(distinguishingArg);
    for (size_t i = 1; i < argsCount; ++i)
        args.append(callFrame->uncheckedArgument(i));

    JSValue target = callFrame->newTarget();
    if (!target) {
        target = globalObject->JSBufferConstructor();
    }

    JSC::JSObject* object = JSC::construct(lexicalGlobalObject, constructor, target, args, "Buffer failed to construct"_s);
    if (!object) {
        return JSC::JSValue::encode({});
    }

    RELEASE_AND_RETURN(throwScope, JSC::JSValue::encode(object));
}

bool JSBuffer__isBuffer(JSC::JSGlobalObject* lexicalGlobalObject, JSC::EncodedJSValue value)
{
    JSC::VM& vm = lexicalGlobalObject->vm();
    auto clientData = WebCore::clientData(vm);

    JSC::JSValue jsValue = JSC::JSValue::decode(value);
    if (!jsValue || !jsValue.isCell())
        return false;

    JSC::JSUint8Array* cell = jsDynamicCast<JSC::JSUint8Array*>(jsValue.asCell());
    if (!cell)
        return false;

    JSValue prototype = cell->getPrototype(vm, lexicalGlobalObject);
    return prototype.inherits<WebCore::JSBufferPrototype>();
}
