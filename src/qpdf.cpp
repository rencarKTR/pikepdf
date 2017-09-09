#include <qpdf/Constants.h>
#include <qpdf/Types.h>
#include <qpdf/DLL.h>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjGen.hh>
#include <qpdf/QPDFXRefEntry.hh>
#include <qpdf/PointerHolder.hh>
#include <qpdf/Buffer.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <sstream>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#if 0
#include <pybind11/eval.h>
#endif

extern "C" const char* qpdf_get_qpdf_version();

using namespace std::literals::string_literals;

namespace py = pybind11;

QPDFObjectHandle objecthandle_encode(py::handle obj);


template <typename T>
void kwargs_to_method(py::kwargs kwargs, const char* key, QPDF* q, void (QPDF::*callback)(T))
{
    try {
        if (kwargs.contains(key)) {
            auto v = kwargs[key].cast<T>();
            (q->*callback)(v);
        }
    } catch (py::cast_error) {
        throw py::type_error(std::string(key) + ": unsupported argument type");
    }
}


/* Convert a Python object to a filesystem encoded path
 * Use Python's os.fspath() which accepts os.PathLike (str, bytes, pathlib.Path)
 * and returns bytes encoded in the filesystem encoding.
 * Cast to a string without transcoding.
 */
std::string fsencode_filename(py::object py_filename)
{
    auto fspath = py::module::import("pikepdf._cpphelpers").attr("fspath");
    std::string filename;

    try {
        auto py_encoded_filename = fspath(py_filename);
        filename = py_encoded_filename.cast<std::string>();
    } catch (py::cast_error) {
        throw py::type_error("expected pathlike object");
    }

    return filename;
}


QPDF* open_pdf(py::args args, py::kwargs kwargs)
{
    QPDF* q = new QPDF();

    if (args.size() < 1) 
        throw py::value_error("not enough arguments");
    if (args.size() > 2)
        throw py::value_error("too many arguments");

    std::string filename = fsencode_filename(args[0]);
    std::string password;

    if (kwargs) {
        if (kwargs.contains("password")) {
            auto v = kwargs["password"].cast<std::string>();
            password = v;
        }
        kwargs_to_method(kwargs, "ignore_xref_streams", q, &QPDF::setIgnoreXRefStreams);
        kwargs_to_method(kwargs, "suppress_warnings", q, &QPDF::setSuppressWarnings);
        kwargs_to_method(kwargs, "attempt_recovery", q, &QPDF::setAttemptRecovery);
    }

    q->setSuppressWarnings(true);
    py::gil_scoped_release release;
    q->processFile(filename.c_str(), password.c_str());
    return q;
}


void init_object(py::module& m);

PYBIND11_MODULE(_qpdf, m) {
    //py::options options;
    //options.disable_function_signatures();

    m.doc() = "pikepdf provides a Pythonic interface for QPDF";

    m.def("qpdf_version", &qpdf_get_qpdf_version, "Get libqpdf version");

    py::register_exception<QPDFExc>(m, "PDFError");

    py::class_<QPDF>(m, "PDF", "In-memory representation of a PDF")
        .def_static("new",
            []() {
                QPDF* q = new QPDF();
                q->emptyPDF();
                q->setSuppressWarnings(true);
                return q;
            },
            "create a new empty PDF from stratch"
        )
        .def_static("open", open_pdf,
            "open existing PDF"
        )
        .def("__repr__",
            [](const QPDF &q) {
                return "<pikepdf.PDF description='"s + q.getFilename() + "'>"s;
            }
        )
        .def_property_readonly("filename", &QPDF::getFilename,
            "the source filename of an existing PDF, when available")
        .def_property_readonly("pdf_version", &QPDF::getPDFVersion,
            "the PDF standard version, such as '1.7'")
        .def_property_readonly("extension_level", &QPDF::getExtensionLevel)
        .def_property_readonly("root", &QPDF::getRoot,
            "the /Root object of the PDF")
        .def_property_readonly("trailer", &QPDF::getTrailer,
            "the PDF trailer")
        .def_property_readonly("pages", &QPDF::getAllPages)
        .def_property_readonly("is_encrypted", &QPDF::isEncrypted)
        .def("get_warnings", &QPDF::getWarnings)  // this is a def because it modifies state by clearing warnings
        .def("show_xref_table", &QPDF::showXRefTable)
        .def("add_page", &QPDF::addPage,
            R"~~~(
            Attach a page to this PDF. The page can be either be a
            newly constructed PDF object or it can be obtained from another
            PDF.

            :param pikepdf.Object page: The page object to attach
            :param bool first: If True, prepend this before the first page; if False append after last page
            )~~~",
            py::arg("page"),
            py::arg("first")
        )
        .def("remove_page", &QPDF::removePage)
        .def("save",
             [](QPDF &q, py::object filename, bool static_id=false,
                bool preserve_pdfa=false) {
                QPDFWriter w(q, fsencode_filename(filename).c_str());
                {
                    py::gil_scoped_release release;
                    if (static_id) {
                        w.setStaticID(true);
                        w.setStreamDataMode(qpdf_s_uncompress);
                    }
                    w.write();
                }
                if (preserve_pdfa) {
                    auto helpers = py::module::import("pikepdf._cpphelpers");
                    helpers.attr("repair_pdfa")(filename);
                }
             },
             "save as a PDF",
             py::arg("filename"),
             py::arg("static_id") = false,
             py::arg("preserve_pdfa") = false
        )
        .def("_get_object_id", &QPDF::getObjectByID)
        .def("make_indirect", &QPDF::makeIndirectObject)
        .def("make_indirect",
            [](QPDF &q, py::object obj) -> QPDFObjectHandle {
                return q.makeIndirectObject(objecthandle_encode(obj));
            }
        )
        .def("_replace_object",
            [](QPDF &q, int objid, int gen, QPDFObjectHandle &h) {
                q.replaceObject(objid, gen, h);
            }
        );

    init_object(m);

#ifdef VERSION_INFO
    m.attr("__version__") = py::str(VERSION_INFO);
#else
    m.attr("__version__") = py::str("dev");
#endif
}