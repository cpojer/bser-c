#include <nan.h>
#include <sstream>
#include <string.h>

/* Return the smallest size int that can store the value */
#define INT_SIZE(x) (((x) == ((int8_t)x))  ? 1 :    \
                     ((x) == ((int16_t)x)) ? 2 :    \
                     ((x) == ((int32_t)x)) ? 4 : 8)

#define BSER_ARRAY     0x00
#define BSER_OBJECT    0x01
#define BSER_STRING    0x02
#define BSER_INT8      0x03
#define BSER_INT16     0x04
#define BSER_INT32     0x05
#define BSER_INT64     0x06
#define BSER_REAL      0x07
#define BSER_TRUE      0x08
#define BSER_FALSE     0x09
#define BSER_NULL      0x0a
#define BSER_TEMPLATE  0x0b
#define BSER_SKIP      0x0c
#define EMPTY_HEADER "\x00\x01\x05\x00\x00\x00\x00"

std::string hex(int to_convert, size_t pad = 2){
  std::string result;
  std::stringstream ss;
  ss << std::hex << to_convert;

  // left-pad
  std::string copy = ss.str();
  while (copy.size() < pad) {
    ss.seekp(0);
    ss << "0" << copy;
    copy = ss.str();
  }

  ss >> result;
  return "0x" + result;
}

// bunser
v8::Local<v8::Value> bunser(const char **ptr, const char *end);

int bunser_int(const char **ptr, const char *end, int64_t *val) {
  int needed;
  const char *buf = *ptr;
  int8_t i8;
  int16_t i16;
  int32_t i32;
  int64_t i64;

  switch (buf[0]) {
    case BSER_INT8:
      needed = 2;
      break;
    case BSER_INT16:
      needed = 3;
      break;
    case BSER_INT32:
      needed = 5;
      break;
    case BSER_INT64:
      needed = 9;
      break;
    default:
      Nan::ThrowTypeError(
        ("bser: invalid bser int encoding " + hex(buf[0]) + ".")
          .c_str()
      );
      return 0;
  }
  if (end - buf < needed) {
    Nan::ThrowTypeError("bser: input buffer to small for int encoding.");
    return 0;
  }
  *ptr = buf + needed;
  switch (buf[0]) {
    case BSER_INT8:
      memcpy(&i8, buf + 1, sizeof(i8));
      *val = i8;
      return 1;
    case BSER_INT16:
      memcpy(&i16, buf + 1, sizeof(i16));
      *val = i16;
      return 1;
    case BSER_INT32:
      memcpy(&i32, buf + 1, sizeof(i32));
      *val = i32;
      return 1;
    case BSER_INT64:
      memcpy(&i64, buf + 1, sizeof(i64));
      *val = i64;
      return 1;
    default:
      return 0;
  }
}

static int bunser_bytestring(const char **ptr, const char *end,
    const char **start, int64_t *len)
{
  const char *buf = *ptr;

  // skip string marker
  buf++;
  if (!bunser_int(&buf, end, len)) {
    return 0;
  }

  if (buf + *len > end) {
    Nan::ThrowTypeError("bser: invalid string length in bser data.");
    return 0;
  }

  *ptr = buf + *len;
  *start = buf;
  return 1;
}

v8::Local<v8::Array> bunser_array(const char **ptr, const char *end) {
  const char *buf = *ptr;
  int64_t length, i;
  v8::Local<v8::Array> arr;

  // skip array header
  buf++;
  if (!bunser_int(&buf, end, &length)) {
    return Nan::New<v8::Array>(0);
  }
  *ptr = buf;

  if (length > LONG_MAX) {
    Nan::ThrowTypeError("bser: array exceeds limits.");
    return Nan::New<v8::Array>(0);
  }

  arr = Nan::New<v8::Array>(length);
  for (i = 0; i < length; i++) {
    v8::Local<v8::Value> element = bunser(ptr, end);
    Nan::Set(arr, i, element);
  }

  return arr;
}

v8::Local<v8::Object> bunser_object(const char **ptr, const char *end) {
  const char *buf = *ptr;
  int64_t length, i;
  v8::Local<v8::Object> obj = Nan::New<v8::Object>();

  // skip object header
  buf++;
  if (!bunser_int(&buf, end, &length)) {
    return obj;
  }
  *ptr = buf;

  for (i = 0; i < length; i++) {
    const char *keystr;
    int64_t keylen;
    v8::Local<v8::String> key;

    if (!bunser_bytestring(ptr, end, &keystr, &keylen)) {
      return obj;
    }

    if (keylen > LONG_MAX) {
      Nan::ThrowTypeError("bser: string exceeds limits.");
      return obj;
    }

    key = Nan::New<v8::String>(keystr, keylen).ToLocalChecked();
    Nan::Set(obj, key, bunser(ptr, end));
  }

  return obj;
}

v8::Local<v8::Array> bunser_template(const char **ptr, const char *end) {
  const char *buf = *ptr;
  v8::Local<v8::Array> arr;
  int64_t length, i;
  int64_t numkeys, keyidx;

  if (buf[1] != BSER_ARRAY) {
    Nan::ThrowTypeError("bser: expected array to follow template.");
    return Nan::New<v8::Array>(0);
  }

  // skip header
  buf++;
  *ptr = buf;

  v8::Local<v8::Array> keys = bunser_array(ptr, end);
  numkeys = keys->Length();
  if (numkeys == 0) {
    return Nan::New<v8::Array>(0);
  }

  // Load number of array elements
  if (!bunser_int(ptr, end, &length)) {
    return Nan::New<v8::Array>(0);
  }

  if (length > LONG_MAX) {
    Nan::ThrowTypeError("bser: object exceeds limits.");
    return Nan::New<v8::Array>(0);
  }

  arr = Nan::New<v8::Array>(length);
  for (i = 0; i < length; i++) {
    v8::Local<v8::Object> obj = Nan::New<v8::Object>();
    for (keyidx = 0; keyidx < numkeys; keyidx++) {
      v8::Local<v8::Value> element;
      if (**ptr == BSER_SKIP) {
        *ptr = *ptr + 1;
        continue;
      } else {
        element = bunser(ptr, end);
      }

      Nan::Set(obj, Nan::Get(keys, keyidx).ToLocalChecked(), element);
    }

    Nan::Set(arr, i, obj);
  }

  return arr;
}

v8::Local<v8::Value> bunser(const char **ptr, const char *end) {
  const char *buf = *ptr;

  switch (buf[0]) {
    case BSER_INT8:
    case BSER_INT16:
    case BSER_INT32:
    case BSER_INT64: {
      int64_t ival;
      if (!bunser_int(ptr, end, &ival)) {
        return Nan::Null();
      }
      // TODO: handle int64
      return Nan::New<v8::Number>(ival);
    }

    case BSER_REAL: {
      double dval;
      memcpy(&dval, buf + 1, sizeof(dval));
      *ptr = buf + 1 + sizeof(double);
      // TODO: handle this
      return Nan::New<v8::Number>(dval);
    }

    case BSER_TRUE:
      *ptr = buf + 1;
      return Nan::True();

    case BSER_FALSE:
      *ptr = buf + 1;
      return Nan::False();

    case BSER_NULL:
      *ptr = buf + 1;
      return Nan::Null();

    case BSER_STRING: {
      const char *start;
      int64_t len;

      if (!bunser_bytestring(ptr, end, &start, &len)) {
        return Nan::Null();
      }

      if (len > LONG_MAX) {
        Nan::ThrowTypeError("bser: string exceeds limits.");
        return Nan::Null();
      }

      return Nan::New<v8::String>(start, len).ToLocalChecked();
    }

    case BSER_ARRAY:
      return bunser_array(ptr, end);

    case BSER_OBJECT:
      return bunser_object(ptr, end);

    case BSER_TEMPLATE:
      return bunser_template(ptr, end);

    default:
      Nan::ThrowTypeError(
        ("bser: unhandled bser opcode " + hex(buf[0]) + ".").c_str()
      );
  }

  return Nan::Null();
}

// API
void bser_dumps(const Nan::FunctionCallbackInfo<v8::Value>& info) {

}

void bser_loads(const Nan::FunctionCallbackInfo<v8::Value>& info) {
  if (info.Length() != 1) {
    Nan::ThrowError("bser.loads: wrong number of arguments.");
    return;
  }

  if (!node::Buffer::HasInstance(info[0])) {
    Nan::ThrowError("bser.loads: argument is nots a buffer.");
    return;
  }

  v8::Local<v8::Object> buffer = info[0]->ToObject();
  const char* data = node::Buffer::Data(buffer);
  size_t bufferLength = node::Buffer::Length(buffer);
  const char *end = data + bufferLength;
  int64_t expectedLength;

  // Validate the header and length
  if (memcmp(data, EMPTY_HEADER, 2) != 0) {
    Nan::ThrowError("bser.loads: invalid bser header.");
    return;
  }

  data += 2;

  // Expect an integer telling us how big the rest of the data
  // should be
  if (!bunser_int(&data, end, &expectedLength)) {
    Nan::ThrowError("bser.loads: invalid bser header.");
    return;
  }

  // Verify
  if (expectedLength + data != end) {
    Nan::ThrowError("bser.loads: bser data len != header len");
    return;
  }

  //v8::Local<v8::Number> num = Nan::New(static_cast<int>(bufferLength));
  info.GetReturnValue().Set(bunser(&data, end));
}

void Init(v8::Local<v8::Object> exports) {
  exports->Set(
    Nan::New("dumps").ToLocalChecked(),
    Nan::New<v8::FunctionTemplate>(bser_dumps)->GetFunction()
  );
  exports->Set(
    Nan::New("loads").ToLocalChecked(),
    Nan::New<v8::FunctionTemplate>(bser_loads)->GetFunction()
  );
}

NODE_MODULE(bser, Init)
