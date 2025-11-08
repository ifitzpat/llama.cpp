;;; guix.scm --- GNU Guix package definitions for llama.cpp GStreamer integration
;;;
;;; This file defines Guix packages for:
;;; - llama-simple: C API wrapper for FFI
;;; - gst-llama: GStreamer plugin for llama.cpp
;;; - cl-llama-simple: Common Lisp bindings (optional)
;;;
;;; Usage:
;;;   guix build -f guix.scm                    # Build all packages
;;;   guix build -f guix.scm llama-simple       # Build C API only
;;;   guix build -f guix.scm gst-llama          # Build GStreamer plugin
;;;   guix shell -D -f guix.scm                 # Enter development shell
;;;

(use-modules (guix packages)
             (guix download)
             (guix git-download)
             (guix build-system cmake)
             (guix build-system meson)
             (guix build-system gnu)
             ((guix licenses) #:prefix license:)
             (gnu packages)
             (gnu packages cmake)
             (gnu packages curl)
             (gnu packages glib)
             (gnu packages gstreamer)
             (gnu packages pkg-config)
             (gnu packages gcc)
             (gnu packages llvm)
             (gnu packages check)
             (gnu packages lisp)
             (gnu packages lisp-xyz)
             (guix gexp))

;;; Helper: Get the current source directory
(define %source-dir (dirname (current-filename)))

;;; Base llama.cpp package
;;; This builds the core libllama and libcommon libraries
(define llama-cpp-base
  (package
    (name "llama-cpp-base")
    (version "git")
    (source (local-file %source-dir
                        #:recursive? #t
                        #:select? (git-predicate %source-dir)))
    (build-system cmake-build-system)
    (arguments
     (list
      #:tests? #f  ; Tests require models
      #:configure-flags
      #~(list "-DGGML_CUDA=OFF"
              "-DGGML_METAL=OFF"
              "-DGGML_VULKAN=OFF"
              "-DBUILD_SHARED_LIBS=ON"
              "-DLLAMA_BUILD_TESTS=OFF"
              "-DLLAMA_BUILD_EXAMPLES=OFF"
              "-DLLAMA_BUILD_SERVER=OFF")
      #:phases
      #~(modify-phases %standard-phases
          (add-after 'unpack 'create-version
            (lambda _
              ;; Create build info files
              (with-output-to-file "common/build-info.cpp"
                (lambda ()
                  (display "int LLAMA_BUILD_NUMBER = 0;\n")
                  (display "char const *LLAMA_COMMIT = \"guix-build\";\n")
                  (display "char const *LLAMA_COMPILER = \"gcc\";\n")
                  (display "char const *LLAMA_BUILD_TARGET = \"native\";\n"))))))))
    (native-inputs
     (list pkg-config cmake))
    (inputs
     (list gcc-toolchain curl))
    (synopsis "Core llama.cpp libraries")
    (description
     "Core llama.cpp libraries (libllama, libcommon) for running LLM inference.")
    (home-page "https://github.com/ggml-org/llama.cpp")
    (license license:expat)))

;;; llama-simple: C API wrapper for FFI
(define-public llama-simple
  (package
    (name "llama-simple")
    (version "1.0.0")
    (source (local-file %source-dir
                        #:recursive? #t
                        #:select? (git-predicate %source-dir)))
    (build-system cmake-build-system)
    (arguments
     (list
      #:tests? #t
      #:configure-flags
      #~(list "-DBUILD_SHARED_LIBS=ON"
              "-DLLAMA_SIMPLE_BUILD_TESTS=ON"
              "-DLLAMA_SIMPLE_BUILD_EXAMPLES=ON")
      #:phases
      #~(modify-phases %standard-phases
          (add-after 'unpack 'set-source-dir
            (lambda _
              ;; CMake expects sources in tools/ffi
              (chdir "tools/ffi")))
          (replace 'check
            (lambda* (#:key tests? #:allow-other-keys)
              (when tests?
                (invoke "ctest" "--output-on-failure")))))))
    (native-inputs
     (list pkg-config cmake))
    (inputs
     (list llama-cpp-base))
    (propagated-inputs
     (list llama-cpp-base))
    (synopsis "Simple C API wrapper for llama.cpp")
    (description
     "Clean C API wrapper around llama.cpp for Foreign Function Interface (FFI)
usage from languages like Common Lisp, Python, Ruby, etc. Provides:
@itemize
@item Model loading/unloading
@item Chat template formatting (Jinja)
@item Token streaming with callbacks
@item Logit bias (token weighting)
@item Pre-sampling hooks
@end itemize")
    (home-page "https://github.com/ggml-org/llama.cpp")
    (license license:expat)))

;;; gst-llama: GStreamer plugin
(define-public gst-llama
  (package
    (name "gst-llama")
    (version "1.0.0")
    (source (local-file %source-dir
                        #:recursive? #t
                        #:select? (git-predicate %source-dir)))
    (build-system meson-build-system)
    (arguments
     (list
      #:tests? #t
      #:configure-flags
      #~(list (string-append "-Dprefix=" #$output)
              "-Dgst-plugin-dir=lib/gstreamer-1.0")
      #:phases
      #~(modify-phases %standard-phases
          (add-after 'unpack 'set-source-dir
            (lambda _
              ;; Meson expects sources in tools/gstreamer
              (chdir "tools/gstreamer")))
          (add-after 'install 'install-examples
            (lambda* (#:key outputs #:allow-other-keys)
              (let* ((out (assoc-ref outputs "out"))
                     (examples (string-append out "/share/examples/gst-llama")))
                (mkdir-p examples)
                (copy-recursively "examples" examples)))))))
    (native-inputs
     (list pkg-config meson ninja))
    (inputs
     (list gstreamer
           gst-plugins-base
           glib
           json-glib
           llama-simple))
    (propagated-inputs
     (list llama-simple))
    (synopsis "GStreamer plugin for llama.cpp text generation")
    (description
     "GStreamer element that wraps llama.cpp for text generation in multimedia
pipelines. Features:
@itemize
@item Standard GStreamer element with sink/src pads
@item Optional control pad for dynamic parameter adjustment
@item Rich signal system for inter-element communication
@item Token-level streaming with metadata
@item Logit bias (token weighting) support
@item Model lifecycle management (load/unload/reload)
@end itemize")
    (home-page "https://github.com/ggml-org/llama.cpp")
    (license license:expat)))

;;; gst-llama-steering: Adaptive steering element (companion)
(define-public gst-llama-steering
  (package
    (inherit gst-llama)
    (name "gst-llama-steering")
    (synopsis "Adaptive steering element for gst-llama")
    (description
     "Companion GStreamer element that monitors gst-llama signals and
dynamically adjusts generation parameters based on model output. Implements
adaptive temperature control based on entropy of logit distributions.")
    (arguments
     (substitute-keyword-arguments (package-arguments gst-llama)
       ((#:configure-flags flags)
        #~(cons "-Dbuild-steering=true" #$flags))))))

;;; cl-llama-simple: Common Lisp bindings (optional)
(define-public cl-llama-simple
  (package
    (name "cl-llama-simple")
    (version "1.0.0")
    (source (local-file %source-dir
                        #:recursive? #t
                        #:select? (git-predicate %source-dir)))
    (build-system gnu-build-system)
    (arguments
     (list
      #:tests? #f  ; Lisp tests run manually
      #:phases
      #~(modify-phases %standard-phases
          (delete 'configure)
          (delete 'build)
          (replace 'install
            (lambda* (#:key outputs #:allow-other-keys)
              (let* ((out (assoc-ref outputs "out"))
                     (lisp-dir (string-append out "/share/common-lisp/source/llama-simple")))
                (mkdir-p lisp-dir)
                (copy-recursively "tools/ffi/lisp" lisp-dir)))))))
    (inputs
     (list llama-simple
           sbcl
           sbcl-cffi))
    (propagated-inputs
     (list llama-simple))
    (synopsis "Common Lisp FFI bindings for llama-simple")
    (description
     "CFFI-based Common Lisp bindings for llama-simple C API. Provides a
high-level, idiomatic Lisp interface for llama.cpp text generation.")
    (home-page "https://github.com/ggml-org/llama.cpp")
    (license license:expat)))

;;; Combined package: Build everything
(define-public llama-gstreamer-all
  (package
    (name "llama-gstreamer-all")
    (version "1.0.0")
    (source #f)
    (build-system gnu-build-system)
    (arguments
     (list
      #:phases
      #~(modify-phases %standard-phases
          (delete 'configure)
          (delete 'build)
          (delete 'check)
          (replace 'install
            (lambda* (#:key outputs #:allow-other-keys)
              (let ((out (assoc-ref outputs "out")))
                (mkdir-p out)
                ;; Create a manifest file
                (with-output-to-file (string-append out "/manifest.txt")
                  (lambda ()
                    (display "llama-gstreamer-all meta-package\n")))))))))
    (propagated-inputs
     (list llama-cpp-base
           llama-simple
           gst-llama
           gst-llama-steering))
    (synopsis "Complete llama.cpp GStreamer integration suite")
    (description
     "Meta-package that installs all llama.cpp GStreamer components:
llama-simple C API, gst-llama plugin, and adaptive steering element.")
    (home-page "https://github.com/ggml-org/llama.cpp")
    (license license:expat)))

;;; Development manifest
;;; Use with: guix shell -D -f guix.scm
(define-public llama-gstreamer-dev
  (package
    (inherit llama-gstreamer-all)
    (name "llama-gstreamer-dev")
    (native-inputs
     (append
      (list
       ;; Build tools
       cmake
       meson
       ninja
       pkg-config
       gcc-toolchain

       ;; Development tools
       clang-toolchain  ; For clang-tidy, clang-format
       valgrind         ; Memory leak detection
       gdb              ; Debugging

       ;; GStreamer development
       gstreamer
       gst-plugins-base
       gst-plugins-good

       ;; Testing
       check)
      (package-native-inputs llama-gstreamer-all)))
    (inputs
     (append
      (list
       ;; Libraries
       glib
       json-glib

       ;; Optional: Lisp development
       sbcl
       sbcl-cffi)
      (package-inputs llama-gstreamer-all)))
    (synopsis "Development environment for llama-gstreamer")
    (description
     "Complete development environment with all tools needed to build and test
the llama.cpp GStreamer integration.")))

;;; Export packages
;;; When invoked with -f, return the development package
llama-gstreamer-dev
