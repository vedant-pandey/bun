import { it, expect, describe } from "bun:test";

import crypto from "node:crypto";
import { PassThrough, Readable } from "node:stream";

it("crypto.randomBytes should return a Buffer", () => {
  expect(crypto.randomBytes(1) instanceof Buffer).toBe(true);
  expect(Buffer.isBuffer(crypto.randomBytes(1))).toBe(true);
});

it("crypto.randomInt should return a number", () => {
  const result = crypto.randomInt(0, 10);
  expect(typeof result).toBe("number");
  expect(result).toBeGreaterThanOrEqual(0);
  expect(result).toBeLessThanOrEqual(10);
});

it("crypto.randomInt with no arguments", () => {
  const result = crypto.randomInt();
  expect(typeof result).toBe("number");
  expect(result).toBeGreaterThanOrEqual(0);
  expect(result).toBeLessThanOrEqual(Number.MAX_SAFE_INTEGER);
});

it("crypto.randomInt with one argument", () => {
  const result = crypto.randomInt(100);
  expect(typeof result).toBe("number");
  expect(result).toBeGreaterThanOrEqual(0);
  expect(result).toBeLessThanOrEqual(100);
});

// https://github.com/oven-sh/bun/issues/1839
describe("createHash", () => {
  it("update & digest", () => {
    const hash = crypto.createHash("sha256");
    hash.update("some data to hash");
    expect(hash.digest("hex")).toBe("6a2da20943931e9834fc12cfe5bb47bbd9ae43489a30726962b576f4e3993e50");
  });

  it("returns Buffer", () => {
    const hash = crypto.createHash("sha256");
    hash.update("some data to hash");
    expect(Buffer.isBuffer(hash.digest())).toBeTrue();
  });

  it("stream (sync)", () => {
    const hash = crypto.createHash("sha256");
    hash.write("some data to hash");
    hash.end();
    expect(hash.read().toString("hex")).toBe("6a2da20943931e9834fc12cfe5bb47bbd9ae43489a30726962b576f4e3993e50");
  });

  it("stream (async)", done => {
    const hash = crypto.createHash("sha256");
    hash.on("readable", () => {
      const data = hash.read();
      if (data) {
        expect(data.toString("hex")).toBe("6a2da20943931e9834fc12cfe5bb47bbd9ae43489a30726962b576f4e3993e50");
        done();
      }
    });
    hash.write("some data to hash");
    hash.end();
  });

  it("stream multiple chunks", done => {
    const hash = crypto.createHash("sha256");
    hash.write("some data to hash");
    hash.on("readable", () => {
      const data = hash.read();
      if (data) {
        expect(data.toString("hex")).toBe("43cc4cdc6bd7799b13da2d7c94bba96f3768bf7c4eba7038e0c393e4474fc9e5");
        done();
      }
    });
    hash.write("some data to hash");
    hash.write("some data to hash");
    hash.end();
  });

  it("stream with pipe", done => {
    const hash = crypto.createHash("sha256");
    const s = new PassThrough();

    hash.on("readable", () => {
      const data = hash.read();
      if (data) {
        expect(data.toString("hex")).toBe("0e1076315962f2e639ba2eea46223a813dafea530425613948c4b21635abd8fc");
        done();
      }
    });
    s.write("Hello world");
    s.pipe(hash);
    s.write("Bun!");
    s.end();
  });

  it("repeated calls doesnt segfault", () => {
    function fn() {
      crypto.createHash("sha1").update(Math.random(), "ascii").digest("base64");
    }

    for (let i = 0; i < 10; i++) fn();
  });

  it("multiple calls to digest throws exception", () => {
    const hash = crypto.createHash("sha256");
    hash.update("hello world");
    expect(hash.digest("hex")).toBe("b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
    expect(() => hash.digest("hex")).toThrow();
  });

  it("copy is the same", () => {
    const hash = crypto.createHash("sha256");
    hash.update("hello");
    const copy = hash.copy();

    expect(copy.digest("hex")).toBe(hash.digest("hex"));
  });

  it("copy is not linked", () => {
    const hash = crypto.createHash("sha256");
    hash.update("hello");
    const copy = hash.copy();

    hash.update("world");
    expect(copy.digest("hex")).not.toBe(hash.digest("hex"));
  });

  it("copy updates the same", () => {
    const hash = crypto.createHash("sha256");
    hash.update("hello");
    const copy = hash.copy();

    hash.update("world");
    copy.update("world");
    expect(copy.digest("hex")).toBe(hash.digest("hex"));
  });
});

it("crypto.createHmac", () => {
  const result = crypto.createHmac("sha256", "key").update("message").digest("base64");

  expect(result).toBe("bp7ym3X//Ft6uuUn1Y/a2y/kLnIZARl2kXNDBl9Y7Uo=");
});

it("web crypto", async () => {
  let bytes = new Uint8Array(32);
  crypto.getRandomValues(bytes);
  await crypto.subtle.digest("SHA-256", bytes);
});

// https://github.com/oven-sh/bun/issues/2110
it("hash regression #2110", () => {
  var s = "6fbf7e2948e0c2f29eaacac1733546a4af5ca482";
  expect(crypto.createHash("sha1").update(s, "binary").digest("hex")).toBe("e7c8b3c6f114c523d07ee355c534ee9bef3c044b");
});
