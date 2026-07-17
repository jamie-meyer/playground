#!/usr/bin/env node

/*
 * Minimal Objective-C method-list reader for 64-bit Mach-O binaries.
 *
 * This intentionally implements only the metadata needed for static analysis:
 * LC_SEGMENT_64 sections, __objc_classlist, class_ro_t, and absolute/relative
 * method_list_t entries. It never loads or executes the inspected binary.
 */

"use strict";

const fs = require("fs");

function fail(message) {
  process.stderr.write(`${message}\n`);
  process.exit(1);
}

if (process.argv.length < 3 || process.argv.length > 4) {
  fail(`usage: ${process.argv[1]} <thin-64-bit-Mach-O> [selector-regex]`);
}

const path = process.argv[2];
let selectorPattern;
try {
  selectorPattern = new RegExp(process.argv[3] ?? ".");
} catch (error) {
  fail(`invalid selector regular expression: ${error.message}`);
}

const bytes = fs.readFileSync(path);
if (bytes.length < 32 || bytes.readUInt32LE(0) !== 0xfeedfacf) {
  fail(`${path}: expected a little-endian, thin 64-bit Mach-O`);
}

const sectionByName = new Map();
const segmentByName = new Map();
const commandCount = bytes.readUInt32LE(16);
let commandOffset = 32;

function fixedString(offset, length) {
  const end = bytes.indexOf(0, offset);
  const boundedEnd = end < 0 || end > offset + length ? offset + length : end;
  return bytes.toString("utf8", offset, boundedEnd);
}

for (let commandIndex = 0; commandIndex < commandCount; commandIndex += 1) {
  const command = bytes.readUInt32LE(commandOffset);
  const commandSize = bytes.readUInt32LE(commandOffset + 4);
  if (commandSize < 8 || commandOffset + commandSize > bytes.length) {
    fail("malformed Mach-O load command");
  }

  if (command === 0x19) {
    const segmentName = fixedString(commandOffset + 8, 16);
    const vmAddress = bytes.readBigUInt64LE(commandOffset + 24);
    const vmSize = bytes.readBigUInt64LE(commandOffset + 32);
    const fileOffset = bytes.readBigUInt64LE(commandOffset + 40);
    const fileSize = bytes.readBigUInt64LE(commandOffset + 48);
    const sectionCount = bytes.readUInt32LE(commandOffset + 64);
    segmentByName.set(segmentName, {
      vmAddress,
      vmSize,
      fileOffset,
      fileSize,
    });

    let sectionOffset = commandOffset + 72;
    for (
      let sectionIndex = 0;
      sectionIndex < sectionCount;
      sectionIndex += 1
    ) {
      const sectionName = fixedString(sectionOffset, 16);
      const ownerName = fixedString(sectionOffset + 16, 16);
      const address = bytes.readBigUInt64LE(sectionOffset + 32);
      const size = bytes.readBigUInt64LE(sectionOffset + 40);
      const offset = BigInt(bytes.readUInt32LE(sectionOffset + 48));
      sectionByName.set(`${ownerName},${sectionName}`, {
        address,
        size,
        offset,
      });
      sectionOffset += 80;
    }
  }
  commandOffset += commandSize;
}

function fileOffsetForAddress(address) {
  for (const segment of segmentByName.values()) {
    if (
      segment.fileSize > 0n &&
      address >= segment.vmAddress &&
      address < segment.vmAddress + segment.vmSize
    ) {
      const relative = address - segment.vmAddress;
      if (relative >= segment.fileSize) {
        fail(`address 0x${address.toString(16)} is not file-backed`);
      }
      return Number(segment.fileOffset + relative);
    }
  }
  fail(`address 0x${address.toString(16)} is outside all segments`);
}

function addressIsMapped(address) {
  for (const segment of segmentByName.values()) {
    if (
      segment.fileSize > 0n &&
      address >= segment.vmAddress &&
      address < segment.vmAddress + segment.vmSize
    ) {
      return true;
    }
  }
  return false;
}

function decodedPointer(rawPointer) {
  if (rawPointer === 0n || addressIsMapped(rawPointer)) {
    return rawPointer;
  }

  /*
   * Newer Mach-O files encode many internal pointers as dyld chained
   * rebase records. Dell's arm64 executable uses the 64-bit offset form:
   * the low 36 bits are an image-relative target and the upper bits hold
   * chain metadata. Binding records are not needed for the class-owned
   * metadata traversed here.
   */
  const textSegment = segmentByName.get("__TEXT");
  if (textSegment === undefined) {
    return rawPointer;
  }
  const imageBase = textSegment.vmAddress;
  if (rawPointer < imageBase) {
    const offsetCandidate = imageBase + rawPointer;
    if (addressIsMapped(offsetCandidate)) {
      return offsetCandidate;
    }
  }
  const target = rawPointer & 0x0000000fffffffffn;
  const candidate = imageBase + target;
  return addressIsMapped(candidate) ? candidate : rawPointer;
}

function pointerAtAddress(address) {
  return decodedPointer(
    bytes.readBigUInt64LE(fileOffsetForAddress(address)),
  );
}

function cStringAtAddress(address) {
  const offset = fileOffsetForAddress(address);
  const end = bytes.indexOf(0, offset);
  if (end < 0) {
    fail(`unterminated string at address 0x${address.toString(16)}`);
  }
  return bytes.toString("utf8", offset, end);
}

function signedRelativeTarget(fieldAddress) {
  const relative = bytes.readInt32LE(fileOffsetForAddress(fieldAddress));
  return fieldAddress + BigInt(relative);
}

function methodsAtAddress(listAddress) {
  if (listAddress === 0n) {
    return [];
  }

  const listOffset = fileOffsetForAddress(listAddress);
  const flagsAndEntrySize = bytes.readUInt32LE(listOffset);
  const entrySize = flagsAndEntrySize & 0x0000fffc;
  const count = bytes.readUInt32LE(listOffset + 4);
  const relative = (flagsAndEntrySize & 0x80000000) !== 0;
  const directSelectors = (flagsAndEntrySize & 0x40000000) !== 0;
  const minimumEntrySize = relative ? 12 : 24;
  if (entrySize < minimumEntrySize || count > 100000) {
    fail(
      `unsupported method list at 0x${listAddress.toString(16)} ` +
        `(flags=0x${flagsAndEntrySize.toString(16)}, count=${count})`,
    );
  }

  const methods = [];
  for (let index = 0; index < count; index += 1) {
    const entryAddress =
      listAddress + 8n + BigInt(index * entrySize);
    let nameAddress;
    let typesAddress;
    let implementationAddress;

    if (relative) {
      nameAddress = signedRelativeTarget(entryAddress);
      if (!directSelectors) {
        nameAddress = pointerAtAddress(nameAddress);
      }
      typesAddress = signedRelativeTarget(entryAddress + 4n);
      implementationAddress = signedRelativeTarget(entryAddress + 8n);
    } else {
      nameAddress = pointerAtAddress(entryAddress);
      typesAddress = pointerAtAddress(entryAddress + 8n);
      implementationAddress = pointerAtAddress(entryAddress + 16n);
    }

    methods.push({
      selector: cStringAtAddress(nameAddress),
      types: cStringAtAddress(typesAddress),
      implementationAddress,
    });
  }
  return methods;
}

function classDescription(classAddress) {
  const classOffset = fileOffsetForAddress(classAddress);
  const metaClassAddress = decodedPointer(
    bytes.readBigUInt64LE(classOffset),
  );
  const dataBits = decodedPointer(
    bytes.readBigUInt64LE(classOffset + 32),
  );
  const classRoAddress = dataBits & ~7n;
  const classRoOffset = fileOffsetForAddress(classRoAddress);
  const nameAddress = decodedPointer(
    bytes.readBigUInt64LE(classRoOffset + 24),
  );
  const methodsAddress = decodedPointer(
    bytes.readBigUInt64LE(classRoOffset + 32),
  );

  let classMethods = [];
  if (metaClassAddress !== 0n) {
    const metaClassOffset = fileOffsetForAddress(metaClassAddress);
    const metaDataBits = decodedPointer(
      bytes.readBigUInt64LE(metaClassOffset + 32),
    );
    const metaRoAddress = metaDataBits & ~7n;
    const metaRoOffset = fileOffsetForAddress(metaRoAddress);
    const metaMethodsAddress = decodedPointer(
      bytes.readBigUInt64LE(metaRoOffset + 32),
    );
    classMethods = methodsAtAddress(metaMethodsAddress);
  }

  return {
    name: cStringAtAddress(nameAddress),
    instanceMethods: methodsAtAddress(methodsAddress),
    classMethods,
  };
}

const classList =
  sectionByName.get("__DATA_CONST,__objc_classlist") ??
  sectionByName.get("__DATA,__objc_classlist");
if (classList === undefined) {
  fail(`${path}: no __objc_classlist section`);
}

const classCount = Number(classList.size / 8n);
for (let index = 0; index < classCount; index += 1) {
  const classAddress = decodedPointer(
    bytes.readBigUInt64LE(
      Number(classList.offset + BigInt(index * 8)),
    ),
  );
  const description = classDescription(classAddress);
  for (const [prefix, methods] of [
    ["-", description.instanceMethods],
    ["+", description.classMethods],
  ]) {
    for (const method of methods) {
      if (selectorPattern.test(method.selector)) {
        process.stdout.write(
          `${prefix}[${description.name} ${method.selector}] ` +
            `imp=0x${method.implementationAddress.toString(16)} ` +
            `types=${method.types}\n`,
        );
      }
      selectorPattern.lastIndex = 0;
    }
  }
}
