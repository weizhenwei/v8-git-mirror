// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <signal.h>
#include <stdio.h>

#include "src/v8.h"

#include "include/libplatform/libplatform.h"
#include "src/assembler.h"
#include "src/base/platform/platform.h"
#include "src/bootstrapper.h"
#include "src/flags.h"
#include "src/list.h"
#include "src/natives.h"
#include "src/serialize.h"


using namespace v8;


class Compressor {
 public:
  virtual ~Compressor() {}
  virtual bool Compress(i::Vector<i::byte> input) = 0;
  virtual i::Vector<i::byte>* output() = 0;
};


class SnapshotWriter {
 public:
  explicit SnapshotWriter(const char* snapshot_file)
      : fp_(GetFileDescriptorOrDie(snapshot_file))
      , raw_file_(NULL)
      , raw_context_file_(NULL)
      , startup_blob_file_(NULL)
      , compressor_(NULL) {
  }

  ~SnapshotWriter() {
    fclose(fp_);
    if (raw_file_) fclose(raw_file_);
    if (raw_context_file_) fclose(raw_context_file_);
    if (startup_blob_file_) fclose(startup_blob_file_);
  }

  void SetCompressor(Compressor* compressor) {
    compressor_ = compressor;
  }

  void SetRawFiles(const char* raw_file, const char* raw_context_file) {
    raw_file_ = GetFileDescriptorOrDie(raw_file);
    raw_context_file_ = GetFileDescriptorOrDie(raw_context_file);
  }

  void SetStartupBlobFile(const char* startup_blob_file) {
    if (startup_blob_file != NULL)
      startup_blob_file_ = GetFileDescriptorOrDie(startup_blob_file);
  }

  void WriteSnapshot(const i::List<i::byte>& snapshot_data,
                     const i::Serializer& serializer,
                     const i::List<i::byte>& context_snapshot_data,
                     const i::Serializer& context_serializer) const {
    WriteSnapshotFile(snapshot_data, serializer,
                      context_snapshot_data, context_serializer);
    MaybeWriteStartupBlob(snapshot_data, serializer,
                          context_snapshot_data, context_serializer);
  }

 private:
  void MaybeWriteStartupBlob(const i::List<i::byte>& snapshot_data,
                             const i::Serializer& serializer,
                             const i::List<i::byte>& context_snapshot_data,
                             const i::Serializer& context_serializer) const {
    if (!startup_blob_file_) return;

    i::SnapshotByteSink sink;

    int spaces[] = {i::NEW_SPACE,           i::OLD_POINTER_SPACE,
                    i::OLD_DATA_SPACE,      i::CODE_SPACE,
                    i::MAP_SPACE,           i::CELL_SPACE,
                    i::PROPERTY_CELL_SPACE, i::LO_SPACE};

    i::byte* snapshot_bytes = snapshot_data.begin();
    sink.PutBlob(snapshot_bytes, snapshot_data.length(), "snapshot");
    for (size_t i = 0; i < arraysize(spaces); ++i) {
      i::Vector<const uint32_t> chunks =
          serializer.FinalAllocationChunks(spaces[i]);
      // For the start-up snapshot, none of the reservations has more than
      // one chunk (reservation for each space fits onto a single page).
      CHECK_EQ(1, chunks.length());
      sink.PutInt(chunks[0], "spaces");
    }

    i::byte* context_bytes = context_snapshot_data.begin();
    sink.PutBlob(context_bytes, context_snapshot_data.length(), "context");
    for (size_t i = 0; i < arraysize(spaces); ++i) {
      i::Vector<const uint32_t> chunks =
          context_serializer.FinalAllocationChunks(spaces[i]);
      // For the context snapshot, none of the reservations has more than
      // one chunk (reservation for each space fits onto a single page).
      CHECK_EQ(1, chunks.length());
      sink.PutInt(chunks[0], "spaces");
    }

    const i::List<i::byte>& startup_blob = sink.data();
    size_t written = fwrite(startup_blob.begin(), 1, startup_blob.length(),
                            startup_blob_file_);
    if (written != static_cast<size_t>(startup_blob.length())) {
      i::PrintF("Writing snapshot file failed.. Aborting.\n");
      exit(1);
    }
  }

  void WriteSnapshotFile(const i::List<i::byte>& snapshot_data,
                         const i::Serializer& serializer,
                         const i::List<i::byte>& context_snapshot_data,
                         const i::Serializer& context_serializer) const {
    WriteFilePrefix();
    WriteData("", snapshot_data, raw_file_);
    WriteData("context_", context_snapshot_data, raw_context_file_);
    WriteMeta("context_", context_serializer);
    WriteMeta("", serializer);
    WriteFileSuffix();
  }

  void WriteFilePrefix() const {
    fprintf(fp_, "// Autogenerated snapshot file. Do not edit.\n\n");
    fprintf(fp_, "#include \"src/v8.h\"\n");
    fprintf(fp_, "#include \"src/base/platform/platform.h\"\n\n");
    fprintf(fp_, "#include \"src/snapshot.h\"\n\n");
    fprintf(fp_, "namespace v8 {\n");
    fprintf(fp_, "namespace internal {\n\n");
  }

  void WriteFileSuffix() const {
    fprintf(fp_, "}  // namespace internal\n");
    fprintf(fp_, "}  // namespace v8\n");
  }

  void WriteData(const char* prefix, const i::List<i::byte>& source_data,
                 FILE* raw_file) const {
    const i::List<i::byte>* data_to_be_written = NULL;
    i::List<i::byte> compressed_data;
    if (!compressor_) {
      data_to_be_written = &source_data;
    } else if (compressor_->Compress(source_data.ToVector())) {
      compressed_data.AddAll(*compressor_->output());
      data_to_be_written = &compressed_data;
    } else {
      i::PrintF("Compression failed. Aborting.\n");
      exit(1);
    }

    DCHECK(data_to_be_written);
    MaybeWriteRawFile(data_to_be_written, raw_file);
    WriteData(prefix, source_data, data_to_be_written);
  }

  void MaybeWriteRawFile(const i::List<i::byte>* data, FILE* raw_file) const {
    if (!data || !raw_file)
      return;

    // Sanity check, whether i::List iterators truly return pointers to an
    // internal array.
    DCHECK(data->end() - data->begin() == data->length());

    size_t written = fwrite(data->begin(), 1, data->length(), raw_file);
    if (written != (size_t)data->length()) {
      i::PrintF("Writing raw file failed.. Aborting.\n");
      exit(1);
    }
  }

  void WriteData(const char* prefix, const i::List<i::byte>& source_data,
                 const i::List<i::byte>* data_to_be_written) const {
    fprintf(fp_, "const byte Snapshot::%sdata_[] = {\n", prefix);
    WriteSnapshotData(data_to_be_written);
    fprintf(fp_, "};\n");
    fprintf(fp_, "const int Snapshot::%ssize_ = %d;\n", prefix,
            data_to_be_written->length());

    if (data_to_be_written == &source_data) {
      fprintf(fp_, "const byte* Snapshot::%sraw_data_ = Snapshot::%sdata_;\n",
              prefix, prefix);
      fprintf(fp_, "const int Snapshot::%sraw_size_ = Snapshot::%ssize_;\n",
              prefix, prefix);
    } else {
      fprintf(fp_, "const byte* Snapshot::%sraw_data_ = NULL;\n", prefix);
      fprintf(fp_, "const int Snapshot::%sraw_size_ = %d;\n",
              prefix, source_data.length());
    }
    fprintf(fp_, "\n");
  }

  void WriteMeta(const char* prefix, const i::Serializer& ser) const {
    WriteSizeVar(ser, prefix, "new", i::NEW_SPACE);
    WriteSizeVar(ser, prefix, "pointer", i::OLD_POINTER_SPACE);
    WriteSizeVar(ser, prefix, "data", i::OLD_DATA_SPACE);
    WriteSizeVar(ser, prefix, "code", i::CODE_SPACE);
    WriteSizeVar(ser, prefix, "map", i::MAP_SPACE);
    WriteSizeVar(ser, prefix, "cell", i::CELL_SPACE);
    WriteSizeVar(ser, prefix, "property_cell", i::PROPERTY_CELL_SPACE);
    WriteSizeVar(ser, prefix, "lo", i::LO_SPACE);
    fprintf(fp_, "\n");
  }

  void WriteSizeVar(const i::Serializer& ser, const char* prefix,
                    const char* name, int space) const {
    i::Vector<const uint32_t> chunks = ser.FinalAllocationChunks(space);
    // For the start-up snapshot, none of the reservations has more than
    // one chunk (total reservation fits into a single page).
    CHECK_EQ(1, chunks.length());
    fprintf(fp_, "const int Snapshot::%s%s_space_used_ = %d;\n", prefix, name,
            chunks[0]);
  }

  void WriteSnapshotData(const i::List<i::byte>* data) const {
    for (int i = 0; i < data->length(); i++) {
      if ((i & 0x1f) == 0x1f)
        fprintf(fp_, "\n");
      if (i > 0)
        fprintf(fp_, ",");
      fprintf(fp_, "%u", static_cast<unsigned char>(data->at(i)));
    }
    fprintf(fp_, "\n");
  }

  FILE* GetFileDescriptorOrDie(const char* filename) {
    FILE* fp = base::OS::FOpen(filename, "wb");
    if (fp == NULL) {
      i::PrintF("Unable to open file \"%s\" for writing.\n", filename);
      exit(1);
    }
    return fp;
  }

  FILE* fp_;
  FILE* raw_file_;
  FILE* raw_context_file_;
  FILE* startup_blob_file_;
  Compressor* compressor_;
};


void DumpException(Handle<Message> message) {
  String::Utf8Value message_string(message->Get());
  String::Utf8Value message_line(message->GetSourceLine());
  fprintf(stderr, "%s at line %d\n", *message_string, message->GetLineNumber());
  fprintf(stderr, "%s\n", *message_line);
  for (int i = 0; i <= message->GetEndColumn(); ++i) {
    fprintf(stderr, "%c", i < message->GetStartColumn() ? ' ' : '^');
  }
  fprintf(stderr, "\n");
}


int main(int argc, char** argv) {
  // By default, log code create information in the snapshot.
  i::FLAG_log_code = true;

  // Print the usage if an error occurs when parsing the command line
  // flags or if the help flag is set.
  int result = i::FlagList::SetFlagsFromCommandLine(&argc, argv, true);
  if (result > 0 || argc != 2 || i::FLAG_help) {
    ::printf("Usage: %s [flag] ... outfile\n", argv[0]);
    i::FlagList::PrintHelp();
    return !i::FLAG_help;
  }

  i::CpuFeatures::Probe(true);
  V8::InitializeICU();
  v8::Platform* platform = v8::platform::CreateDefaultPlatform();
  v8::V8::InitializePlatform(platform);
  v8::V8::Initialize();

  i::FLAG_logfile_per_isolate = false;

  Isolate::CreateParams params;
  params.enable_serializer = true;
  Isolate* isolate = v8::Isolate::New(params);
  { Isolate::Scope isolate_scope(isolate);
    i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate);

    Persistent<Context> context;
    {
      HandleScope handle_scope(isolate);
      context.Reset(isolate, Context::New(isolate));
    }

    if (context.IsEmpty()) {
      fprintf(stderr,
              "\nException thrown while compiling natives - see above.\n\n");
      exit(1);
    }
    if (i::FLAG_extra_code != NULL) {
      // Capture 100 frames if anything happens.
      V8::SetCaptureStackTraceForUncaughtExceptions(true, 100);
      HandleScope scope(isolate);
      v8::Context::Scope cscope(v8::Local<v8::Context>::New(isolate, context));
      const char* name = i::FLAG_extra_code;
      FILE* file = base::OS::FOpen(name, "rb");
      if (file == NULL) {
        fprintf(stderr, "Failed to open '%s': errno %d\n", name, errno);
        exit(1);
      }

      fseek(file, 0, SEEK_END);
      int size = ftell(file);
      rewind(file);

      char* chars = new char[size + 1];
      chars[size] = '\0';
      for (int i = 0; i < size;) {
        int read = static_cast<int>(fread(&chars[i], 1, size - i, file));
        if (read < 0) {
          fprintf(stderr, "Failed to read '%s': errno %d\n", name, errno);
          exit(1);
        }
        i += read;
      }
      fclose(file);
      Local<String> source = String::NewFromUtf8(isolate, chars);
      TryCatch try_catch;
      Local<Script> script = Script::Compile(source);
      if (try_catch.HasCaught()) {
        fprintf(stderr, "Failure compiling '%s'\n", name);
        DumpException(try_catch.Message());
        exit(1);
      }
      script->Run();
      if (try_catch.HasCaught()) {
        fprintf(stderr, "Failure running '%s'\n", name);
        DumpException(try_catch.Message());
        exit(1);
      }
    }
    // Make sure all builtin scripts are cached.
    { HandleScope scope(isolate);
      for (int i = 0; i < i::Natives::GetBuiltinsCount(); i++) {
        internal_isolate->bootstrapper()->NativesSourceLookup(i);
      }
    }
    // If we don't do this then we end up with a stray root pointing at the
    // context even after we have disposed of the context.
    internal_isolate->heap()->CollectAllAvailableGarbage("mksnapshot");
    i::Object* raw_context = *v8::Utils::OpenPersistent(context);
    context.Reset();

    // This results in a somewhat smaller snapshot, probably because it gets
    // rid of some things that are cached between garbage collections.
    i::SnapshotByteSink snapshot_sink;
    i::StartupSerializer ser(internal_isolate, &snapshot_sink);
    ser.SerializeStrongReferences();

    i::SnapshotByteSink context_sink;
    i::PartialSerializer context_ser(internal_isolate, &ser, &context_sink);
    context_ser.Serialize(&raw_context);
    ser.SerializeWeakReferences();

    context_ser.FinalizeAllocation();
    ser.FinalizeAllocation();

    {
      SnapshotWriter writer(argv[1]);
      if (i::FLAG_raw_file && i::FLAG_raw_context_file)
        writer.SetRawFiles(i::FLAG_raw_file, i::FLAG_raw_context_file);
      if (i::FLAG_startup_blob)
        writer.SetStartupBlobFile(i::FLAG_startup_blob);
      writer.WriteSnapshot(snapshot_sink.data(), ser, context_sink.data(),
                           context_ser);
    }
  }

  isolate->Dispose();
  V8::Dispose();
  V8::ShutdownPlatform();
  delete platform;
  return 0;
}
