pub usingnamespace @import("./webcore/response.zig");
pub usingnamespace @import("./webcore/encoding.zig");
pub usingnamespace @import("./webcore/streams.zig");
pub usingnamespace @import("./webcore/blob.zig");
pub usingnamespace @import("./webcore/request.zig");
pub usingnamespace @import("./webcore/body.zig");

const JSC = @import("root").bun.JSC;
const std = @import("std");
const bun = @import("root").bun;
const string = bun.string;
pub const AbortSignal = @import("./bindings/bindings.zig").AbortSignal;
pub const JSValue = @import("./bindings/bindings.zig").JSValue;

pub const Lifetime = enum {
    clone,
    transfer,
    share,
    /// When reading from a fifo like STDIN/STDERR
    temporary,
};

pub const Alert = struct {
    pub const Class = JSC.NewClass(
        void,
        .{ .name = "alert" },
        .{
            .call = .{ .rfn = call },
        },
        .{},
    );

    /// https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html#dom-alert
    pub fn call(
        // this
        _: void,
        ctx: JSC.C.JSContextRef,
        // function
        _: JSC.C.JSObjectRef,
        // thisObject
        _: JSC.C.JSObjectRef,
        arguments: []const JSC.C.JSValueRef,
        _: JSC.C.ExceptionRef,
    ) JSC.C.JSValueRef {
        var output = bun.Output.writer();
        const has_message = arguments.len != 0;

        // 2. If the method was invoked with no arguments, then let message be the empty string; otherwise, let message be the method's first argument.
        if (has_message) {
            var state = std.heap.stackFallback(2048, bun.default_allocator);
            const allocator = state.get();
            const message = arguments[0].?.value().toSlice(ctx.ptr(), allocator);
            defer message.deinit();

            // 3. Set message to the result of normalizing newlines given message.
            // *  We skip step 3 because they are already done in most terminals by default.

            // 4. Set message to the result of optionally truncating message.
            // *  We just don't do this because it's not necessary.

            // 5. Show message to the user, treating U+000A LF as a line break.
            output.writeAll(message.slice()) catch {
                // 1. If we cannot show simple dialogs for this, then return.
                return JSC.JSValue.jsUndefined().asObjectRef();
            };
        }

        output.writeAll(if (has_message) " [Enter] " else "Alert [Enter] ") catch {
            // 1. If we cannot show simple dialogs for this, then return.
            return JSC.JSValue.jsUndefined().asObjectRef();
        };

        // 6. Invoke WebDriver BiDi user prompt opened with this, "alert", and message.
        // *  Not pertinent to use their complex system in a server context.
        bun.Output.flush();

        // 7. Optionally, pause while waiting for the user to acknowledge the message.
        var stdin = std.io.getStdIn();
        var reader = stdin.reader();
        while (true) {
            const byte = reader.readByte() catch break;
            if (byte == '\n') break;
        }

        // 8. Invoke WebDriver BiDi user prompt closed with this and true.
        // *  Again, not necessary in a server context.

        return JSC.JSValue.jsUndefined().asObjectRef();
    }
};

pub const Confirm = struct {
    pub const Class = JSC.NewClass(
        void,
        .{ .name = "confirm" },
        .{
            .call = .{ .rfn = call },
        },
        .{},
    );

    /// https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html#dom-confirm
    pub fn call(
        // this
        _: void,
        ctx: JSC.C.JSContextRef,
        // function
        _: JSC.C.JSObjectRef,
        // thisObject
        _: JSC.C.JSObjectRef,
        arguments: []const JSC.C.JSValueRef,
        _: JSC.C.ExceptionRef,
    ) JSC.C.JSValueRef {
        var output = bun.Output.writer();
        const has_message = arguments.len != 0;

        if (has_message) {
            var state = std.heap.stackFallback(1024, bun.default_allocator);
            const allocator = state.get();
            // 2. Set message to the result of normalizing newlines given message.
            // *  Not pertinent to a server runtime so we will just let the terminal handle this.

            // 3. Set message to the result of optionally truncating message.
            // *  Not necessary so we won't do it.
            const message = arguments[0].?.value().toSlice(ctx.ptr(), allocator);
            defer message.deinit();

            output.writeAll(message.slice()) catch {
                // 1. If we cannot show simple dialogs for this, then return false.
                return JSC.JSValue.jsBoolean(false).asObjectRef();
            };
        }

        // 4. Show message to the user, treating U+000A LF as a line break,
        //    and ask the user to respond with a positive or negative
        //    response.
        output.writeAll(if (has_message) " [y/N] " else "Confirm [y/N] ") catch {
            // 1. If we cannot show simple dialogs for this, then return false.
            return JSC.JSValue.jsBoolean(false).asObjectRef();
        };

        // 5. Invoke WebDriver BiDi user prompt opened with this, "confirm", and message.
        // *  Not relevant in a server context.
        bun.Output.flush();

        // 6. Pause until the user responds either positively or negatively.
        var stdin = std.io.getStdIn();
        var reader = stdin.reader();

        const first_byte = reader.readByte() catch {
            return JSC.JSValue.jsBoolean(false).asObjectRef();
        };

        // 7. Invoke WebDriver BiDi user prompt closed with this, and true if
        //    the user responded positively or false otherwise.
        // *  Not relevant in a server context.

        switch (first_byte) {
            '\n' => return JSC.JSValue.jsBoolean(false).asObjectRef(),
            'y', 'Y' => {
                const next_byte = reader.readByte() catch {
                    // They may have said yes, but the stdin is invalid.
                    return JSC.JSValue.jsBoolean(false).asObjectRef();
                };

                if (next_byte == '\n') {
                    // 8. If the user responded positively, return true;
                    //    otherwise, the user responded negatively: return false.
                    return JSC.JSValue.jsBoolean(true).asObjectRef();
                }
            },
            else => {},
        }

        while (reader.readByte()) |b| {
            if (b == '\n') break;
        } else |_| {}

        // 8. If the user responded positively, return true; otherwise, the user
        //    responded negatively: return false.
        return JSC.JSValue.jsBoolean(false).asObjectRef();
    }
};

pub const Prompt = struct {
    pub const Class = JSC.NewClass(
        void,
        .{ .name = "prompt" },
        .{
            .call = .{ .rfn = call },
        },
        .{},
    );

    /// Adapted from `std.io.Reader.readUntilDelimiterArrayList` to only append
    /// and assume capacity.
    pub fn readUntilDelimiterArrayListAppendAssumeCapacity(
        reader: anytype,
        array_list: *std.ArrayList(u8),
        delimiter: u8,
        max_size: usize,
    ) !void {
        while (true) {
            if (array_list.items.len == max_size) {
                return error.StreamTooLong;
            }

            var byte: u8 = try reader.readByte();

            if (byte == delimiter) {
                return;
            }

            array_list.appendAssumeCapacity(byte);
        }
    }

    /// Adapted from `std.io.Reader.readUntilDelimiterArrayList` to always append
    /// and not resize.
    fn readUntilDelimiterArrayListInfinity(
        reader: anytype,
        array_list: *std.ArrayList(u8),
        delimiter: u8,
    ) !void {
        while (true) {
            var byte: u8 = try reader.readByte();

            if (byte == delimiter) {
                return;
            }

            try array_list.append(byte);
        }
    }

    /// https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html#dom-prompt
    pub fn call(
        // this
        _: void,
        ctx: JSC.C.JSContextRef,
        // function
        _: JSC.C.JSObjectRef,
        // thisObject
        _: JSC.C.JSObjectRef,
        arguments: []const JSC.C.JSValueRef,
        _: JSC.C.ExceptionRef,
    ) JSC.C.JSValueRef {
        var state = std.heap.stackFallback(2048, bun.default_allocator);
        const allocator = state.get();
        var output = bun.Output.writer();
        const has_message = arguments.len != 0;
        const has_default = arguments.len >= 2;
        // 4. Set default to the result of optionally truncating default.
        // *  We don't really need to do this.
        const default = if (has_default) arguments[1] else JSC.JSValue.jsNull().asObjectRef();

        if (has_message) {
            // 2. Set message to the result of normalizing newlines given message.
            // *  Not pertinent to a server runtime so we will just let the terminal handle this.

            // 3. Set message to the result of optionally truncating message.
            // *  Not necessary so we won't do it.
            const message = arguments[0].?.value().toSlice(ctx.ptr(), allocator);
            defer message.deinit();

            output.writeAll(message.slice()) catch {
                // 1. If we cannot show simple dialogs for this, then return null.
                return JSC.JSValue.jsNull().asObjectRef();
            };
        }

        // 4. Set default to the result of optionally truncating default.

        // 5. Show message to the user, treating U+000A LF as a line break,
        //    and ask the user to either respond with a string value or
        //    abort. The response must be defaulted to the value given by
        //    default.
        output.writeAll(if (has_message) " " else "Prompt ") catch {
            // 1. If we cannot show simple dialogs for this, then return false.
            return JSC.JSValue.jsBoolean(false).asObjectRef();
        };

        if (has_default) {
            const default_string = arguments[1].?.value().toSlice(ctx.ptr(), allocator);
            defer default_string.deinit();

            output.print("[{s}] ", .{default_string.slice()}) catch {
                // 1. If we cannot show simple dialogs for this, then return false.
                return JSC.JSValue.jsBoolean(false).asObjectRef();
            };
        }

        // 6. Invoke WebDriver BiDi user prompt opened with this, "prompt" and message.
        // *  Not relevant in a server context.
        bun.Output.flush();

        // 7. Pause while waiting for the user's response.
        var stdin = std.io.getStdIn();
        var reader = stdin.reader();

        const first_byte = reader.readByte() catch {
            // 8. Let result be null if the user aborts, or otherwise the string
            //    that the user responded with.
            return JSC.JSValue.jsNull().asObjectRef();
        };

        if (first_byte == '\n') {
            // 8. Let result be null if the user aborts, or otherwise the string
            //    that the user responded with.
            return default;
        }

        var input = std.ArrayList(u8).initCapacity(allocator, 2048) catch {
            // 8. Let result be null if the user aborts, or otherwise the string
            //    that the user responded with.
            return JSC.JSValue.jsNull().asObjectRef();
        };
        defer input.deinit();

        input.appendAssumeCapacity(first_byte);

        // All of this code basically just first tries to load the input into a
        // buffer of size 2048. If that is too small, then increase the buffer
        // size to 4096. If that is too small, then just dynamically allocate
        // the rest.
        readUntilDelimiterArrayListAppendAssumeCapacity(reader, &input, '\n', 2048) catch |e| {
            if (e != error.StreamTooLong) {
                // 8. Let result be null if the user aborts, or otherwise the string
                //    that the user responded with.
                return JSC.JSValue.jsNull().asObjectRef();
            }

            input.ensureTotalCapacity(4096) catch {
                // 8. Let result be null if the user aborts, or otherwise the string
                //    that the user responded with.
                return JSC.JSValue.jsNull().asObjectRef();
            };

            readUntilDelimiterArrayListAppendAssumeCapacity(reader, &input, '\n', 4096) catch |e2| {
                if (e2 != error.StreamTooLong) {
                    // 8. Let result be null if the user aborts, or otherwise the string
                    //    that the user responded with.
                    return JSC.JSValue.jsNull().asObjectRef();
                }

                readUntilDelimiterArrayListInfinity(reader, &input, '\n') catch {
                    // 8. Let result be null if the user aborts, or otherwise the string
                    //    that the user responded with.
                    return JSC.JSValue.jsNull().asObjectRef();
                };
            };
        };

        // 8. Let result be null if the user aborts, or otherwise the string
        //    that the user responded with.
        var result = JSC.ZigString.init(input.items);
        result.markUTF8();

        // 9. Invoke WebDriver BiDi user prompt closed with this, false if
        //    result is null or true otherwise, and result.
        // *  Too complex for server context.

        // 9. Return result.
        return result.toValueGC(ctx.ptr()).asObjectRef();
    }
};

pub const Crypto = struct {
    const BoringSSL = @import("root").bun.BoringSSL;
    pub const Class = JSC.NewClass(
        void,
        .{ .name = "crypto" },
        .{
            .getRandomValues = JSC.DOMCall("Crypto", @This(), "getRandomValues", JSC.JSValue, JSC.DOMEffect.top),
            .randomUUID = JSC.DOMCall("Crypto", @This(), "randomUUID", *JSC.JSString, JSC.DOMEffect.top),
            .timingSafeEqual = JSC.DOMCall("Crypto", @This(), "timingSafeEqual", JSC.JSValue, JSC.DOMEffect.top),
            .randomInt = .{ .rfn = &JSC.wrapWithHasContainer(Crypto, "randomInt", false, false, false) },
            .scryptSync = .{ .rfn = &JSC.wrapWithHasContainer(Crypto, "scryptSync", false, false, false) },
        },
        .{},
    );
    pub const Prototype = JSC.NewClass(
        void,
        .{ .name = "Crypto" },
        .{
            .call = .{
                .rfn = call,
            },
        },
        .{},
    );

    pub fn scryptSync(
        globalThis: *JSC.JSGlobalObject,
        password: JSC.Node.StringOrBuffer,
        salt: JSC.Node.StringOrBuffer,
        keylen_value: JSC.JSValue,
        options: ?JSC.JSValue,
    ) JSC.JSValue {
        const password_string = password.slice();
        const salt_string = salt.slice();

        if (keylen_value.isEmptyOrUndefinedOrNull() or !keylen_value.isAnyInt()) {
            const err = globalThis.createInvalidArgs("keylen must be an integer", .{});
            globalThis.throwValue(err);
            return .zero;
        }

        const keylen_int = keylen_value.to(i64);
        if (keylen_int < 0) {
            globalThis.throwValue(globalThis.createRangeError("keylen must be a positive integer", .{}));
            return .zero;
        } else if (keylen_int > 0x7fffffff) {
            globalThis.throwValue(globalThis.createRangeError("keylen must be less than 2^31", .{}));
            return .zero;
        }

        var blockSize: ?usize = null;
        var cost: ?usize = null;
        var parallelization: ?usize = null;
        var maxmem: usize = 32 * 1024 * 1024;
        const keylen = @intCast(u32, @truncate(i33, keylen_int));

        if (options) |options_value| outer: {
            if (options_value.isUndefined() or options_value == .zero)
                break :outer;

            if (!options_value.isObject()) {
                globalThis.throwValue(globalThis.createInvalidArgs("options must be an object", .{}));
                return .zero;
            }

            if (options_value.getTruthy(globalThis, "cost") orelse options_value.get(globalThis, "N")) |N_value| {
                if (cost != null) return throwInvalidParameter(globalThis);
                const N_int = N_value.to(i64);
                if (N_int < 0 or !N_value.isNumber()) {
                    return throwInvalidParams(
                        globalThis,
                        .RangeError,
                        "Invalid scrypt params\n\n N must be a positive integer\n",
                        .{},
                    );
                } else if (N_int != 0) {
                    cost = @intCast(usize, N_int);
                }
            }

            if (options_value.getTruthy(globalThis, "blockSize") orelse options_value.get(globalThis, "r")) |r_value| {
                if (blockSize != null) return throwInvalidParameter(globalThis);
                const r_int = r_value.to(i64);
                if (r_int < 0 or !r_value.isNumber()) {
                    return throwInvalidParams(
                        globalThis,
                        .RangeError,
                        "Invalid scrypt params\n\n r must be a positive integer\n",
                        .{},
                    );
                } else if (r_int != 0) {
                    blockSize = @intCast(usize, r_int);
                }
            }

            if (options_value.getTruthy(globalThis, "parallelization") orelse options_value.get(globalThis, "p")) |p_value| {
                if (parallelization != null) return throwInvalidParameter(globalThis);
                const p_int = p_value.to(i64);
                if (p_int < 0 or !p_value.isNumber()) {
                    return throwInvalidParams(
                        globalThis,
                        .RangeError,
                        "Invalid scrypt params\n\n p must be a positive integer\n",
                        .{},
                    );
                } else if (p_int != 0) {
                    parallelization = @intCast(usize, p_int);
                }
            }

            if (options_value.getTruthy(globalThis, "maxmem")) |value| {
                const p_int = value.to(i64);
                if (p_int < 0 or !value.isNumber()) {
                    return throwInvalidParams(
                        globalThis,
                        .RangeError,
                        "Invalid scrypt params\n\n N must be a positive integer\n",
                        .{},
                    );
                } else if (p_int != 0) {
                    maxmem = @intCast(usize, p_int);
                }
            }
        }

        if (blockSize == null) blockSize = 8;
        if (cost == null) cost = 16384;
        if (parallelization == null) parallelization = 1;

        if (cost.? < 2 or cost.? > 0x3fffffff) {
            return throwInvalidParams(
                globalThis,
                .RangeError,
                "Invalid scrypt params\n\n N must be greater than 1 and less than 2^30\n",
                .{},
            );
        }

        if (cost.? == 0 or (cost.? & (cost.? - 1)) != 0) {
            return throwInvalidParams(
                globalThis,
                .RangeError,
                "Invalid scrypt params\n\n N must be a power of 2 greater than 1\n",
                .{},
            );
        }

        if (keylen == 0) {
            if ((BoringSSL.EVP_PBE_scrypt(
                null,
                0,
                null,
                0,
                cost.?,
                blockSize.?,
                parallelization.?,
                maxmem,
                null,
                0,
            ) != 1)) {
                return throwInvalidParams(globalThis, .RangeError, "Invalid scrypt params\n", .{});
            }

            return JSC.ArrayBuffer.createEmpty(globalThis, .ArrayBuffer);
        }

        var stackbuf: [1024]u8 = undefined;
        var buf: []u8 = &stackbuf;
        var needs_deinit = false;
        defer if (needs_deinit) globalThis.allocator().free(buf);
        if (keylen > buf.len) {
            // i don't think its a real scenario, but just in case
            buf = globalThis.allocator().alloc(u8, keylen) catch {
                globalThis.throw("Failed to allocate memory", .{});
                return JSC.JSValue.jsUndefined();
            };
            needs_deinit = true;
        } else {
            buf.len = keylen;
        }

        if (BoringSSL.EVP_PBE_scrypt(
            password_string.ptr,
            password_string.len,
            salt_string.ptr,
            salt_string.len,
            cost.?,
            blockSize.?,
            parallelization.?,
            maxmem,
            buf.ptr,
            keylen,
        ) != 1) {
            return throwInvalidParams(globalThis, .RangeError, "Invalid scrypt params\n", .{});
        }

        return JSC.ArrayBuffer.create(globalThis, buf, .ArrayBuffer);
    }

    fn throwInvalidParameter(globalThis: *JSC.JSGlobalObject) JSC.JSValue {
        const err = globalThis.createErrorInstanceWithCode(
            .ERR_CRYPTO_SCRYPT_INVALID_PARAMETER,
            "Invalid scrypt parameters",
            .{},
        );
        globalThis.throwValue(err);
        return .zero;
    }

    fn throwInvalidParams(globalThis: *JSC.JSGlobalObject, comptime error_type: @Type(.EnumLiteral), comptime message: string, fmt: anytype) JSC.JSValue {
        const err = switch (error_type) {
            .RangeError => globalThis.createRangeErrorInstanceWithCode(
                .ERR_CRYPTO_INVALID_SCRYPT_PARAMS,
                message,
                fmt,
            ),
            else => @compileError("Error type not added!"),
        };
        globalThis.throwValue(err);
        BoringSSL.ERR_clear_error();
        return .zero;
    }

    pub fn timingSafeEqual(
        globalThis: *JSC.JSGlobalObject,
        _: JSC.JSValue,
        arguments: []const JSC.JSValue,
    ) JSC.JSValue {
        if (arguments.len < 2) {
            globalThis.throwInvalidArguments("Expected 2 typed arrays but got nothing", .{});
            return JSC.JSValue.jsUndefined();
        }

        const array_buffer_a = arguments[0].asArrayBuffer(globalThis) orelse {
            globalThis.throwInvalidArguments("Expected typed array but got {s}", .{@tagName(arguments[0].jsType())});
            return JSC.JSValue.jsUndefined();
        };
        const a = array_buffer_a.byteSlice();

        const array_buffer_b = arguments[1].asArrayBuffer(globalThis) orelse {
            globalThis.throwInvalidArguments("Expected typed array but got {s}", .{@tagName(arguments[1].jsType())});
            return JSC.JSValue.jsUndefined();
        };
        const b = array_buffer_b.byteSlice();

        const len = a.len;
        if (b.len != len) {
            globalThis.throw("Input buffers must have the same byte length", .{});
            return JSC.JSValue.jsUndefined();
        }
        return JSC.jsBoolean(len == 0 or bun.BoringSSL.CRYPTO_memcmp(a.ptr, b.ptr, len) == 0);
    }

    pub fn timingSafeEqualWithoutTypeChecks(
        globalThis: *JSC.JSGlobalObject,
        _: *anyopaque,
        array_a: *JSC.JSUint8Array,
        array_b: *JSC.JSUint8Array,
    ) callconv(.C) JSC.JSValue {
        const a = array_a.slice();
        const b = array_b.slice();

        const len = a.len;
        if (b.len != len) {
            globalThis.throw("Input buffers must have the same byte length", .{});
            return .zero;
        }

        return JSC.jsBoolean(len == 0 or bun.BoringSSL.CRYPTO_memcmp(a.ptr, b.ptr, len) == 0);
    }

    pub fn getRandomValues(
        globalThis: *JSC.JSGlobalObject,
        _: JSC.JSValue,
        arguments: []const JSC.JSValue,
    ) JSC.JSValue {
        if (arguments.len == 0) {
            globalThis.throwInvalidArguments("Expected typed array but got nothing", .{});
            return JSC.JSValue.jsUndefined();
        }

        var array_buffer = arguments[0].asArrayBuffer(globalThis) orelse {
            globalThis.throwInvalidArguments("Expected typed array but got {s}", .{@tagName(arguments[0].jsType())});
            return JSC.JSValue.jsUndefined();
        };
        var slice = array_buffer.byteSlice();

        randomData(globalThis, slice.ptr, slice.len);

        return arguments[0];
    }

    pub fn getRandomValuesWithoutTypeChecks(
        globalThis: *JSC.JSGlobalObject,
        _: *anyopaque,
        array: *JSC.JSUint8Array,
    ) callconv(.C) JSC.JSValue {
        var slice = array.slice();
        randomData(globalThis, slice.ptr, slice.len);
        return @enumFromInt(JSC.JSValue, @bitCast(i64, @intFromPtr(array)));
    }

    fn randomData(
        globalThis: *JSC.JSGlobalObject,
        ptr: [*]u8,
        len: usize,
    ) void {
        var slice = ptr[0..len];

        switch (slice.len) {
            0 => {},
            // 512 bytes or less we reuse from the same cache as UUID generation.
            1...JSC.RareData.EntropyCache.size / 8 => {
                bun.copy(u8, slice, globalThis.bunVM().rareData().entropySlice(slice.len));
            },
            else => {
                bun.rand(slice);
            },
        }
    }

    pub fn randomUUID(
        globalThis: *JSC.JSGlobalObject,
        _: JSC.JSValue,
        _: []const JSC.JSValue,
    ) JSC.JSValue {
        var out: [36]u8 = undefined;
        const uuid = globalThis.bunVM().rareData().nextUUID();

        uuid.print(&out);
        return JSC.ZigString.init(&out).toValueGC(globalThis);
    }

    pub fn randomInt(globalThis: *JSC.JSGlobalObject, min_value: ?JSValue, max_value: ?JSValue) JSValue {
        _ = globalThis;

        var at_least: u52 = 0;
        var at_most: u52 = std.math.maxInt(u52);

        if (min_value) |min| {
            if (max_value) |max| {
                if (min.isNumber()) at_least = min.to(u52);
                if (max.isNumber()) at_most = max.to(u52);
            } else {
                if (min.isNumber()) at_most = min.to(u52);
            }
        }

        return JSValue.jsNumberFromUint64(std.crypto.random.intRangeAtMost(u52, at_least, at_most));
    }

    pub fn randomUUIDWithoutTypeChecks(
        globalThis: *JSC.JSGlobalObject,
        _: *anyopaque,
    ) callconv(.C) JSC.JSValue {
        var out: [36]u8 = undefined;
        const uuid = globalThis.bunVM().rareData().nextUUID();

        uuid.print(&out);
        return JSC.ZigString.init(&out).toValueGC(globalThis);
    }

    pub fn call(
        // this
        _: void,
        _: JSC.C.JSContextRef,
        // function
        _: JSC.C.JSObjectRef,
        // thisObject
        _: JSC.C.JSObjectRef,
        _: []const JSC.C.JSValueRef,
        _: JSC.C.ExceptionRef,
    ) JSC.C.JSValueRef {
        return JSC.JSValue.jsUndefined().asObjectRef();
    }
};
