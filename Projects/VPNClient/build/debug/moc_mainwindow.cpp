/****************************************************************************
** Meta object code from reading C++ file 'mainwindow.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.5.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../mainwindow.h"
#include <QtCore/qmetatype.h>

#if __has_include(<QtCore/qtmochelpers.h>)
#include <QtCore/qtmochelpers.h>
#else
QT_BEGIN_MOC_NAMESPACE
#endif


#include <memory>

#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mainwindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.5.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {

#ifdef QT_MOC_HAS_STRINGDATA
struct qt_meta_stringdata_CLASSMainWindowENDCLASS_t {};
static constexpr auto qt_meta_stringdata_CLASSMainWindowENDCLASS = QtMocHelpers::stringData(
    "MainWindow",
    "on_browseCaCert_clicked",
    "",
    "on_browseClientCert_clicked",
    "on_browseClientKey_clicked",
    "on_connectButton_clicked",
    "on_disconnectButton_clicked",
    "on_refreshInterfaces_clicked",
    "readProcessOutput",
    "processFinished",
    "exitCode",
    "QProcess::ExitStatus",
    "exitStatus",
    "on_testConnectionBtn_clicked",
    "on_testProcessOutput",
    "on_testProcessFinished"
);
#else  // !QT_MOC_HAS_STRING_DATA
struct qt_meta_stringdata_CLASSMainWindowENDCLASS_t {
    uint offsetsAndSizes[32];
    char stringdata0[11];
    char stringdata1[24];
    char stringdata2[1];
    char stringdata3[28];
    char stringdata4[27];
    char stringdata5[25];
    char stringdata6[28];
    char stringdata7[29];
    char stringdata8[18];
    char stringdata9[16];
    char stringdata10[9];
    char stringdata11[21];
    char stringdata12[11];
    char stringdata13[29];
    char stringdata14[21];
    char stringdata15[23];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_CLASSMainWindowENDCLASS_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_CLASSMainWindowENDCLASS_t qt_meta_stringdata_CLASSMainWindowENDCLASS = {
    {
        QT_MOC_LITERAL(0, 10),  // "MainWindow"
        QT_MOC_LITERAL(11, 23),  // "on_browseCaCert_clicked"
        QT_MOC_LITERAL(35, 0),  // ""
        QT_MOC_LITERAL(36, 27),  // "on_browseClientCert_clicked"
        QT_MOC_LITERAL(64, 26),  // "on_browseClientKey_clicked"
        QT_MOC_LITERAL(91, 24),  // "on_connectButton_clicked"
        QT_MOC_LITERAL(116, 27),  // "on_disconnectButton_clicked"
        QT_MOC_LITERAL(144, 28),  // "on_refreshInterfaces_clicked"
        QT_MOC_LITERAL(173, 17),  // "readProcessOutput"
        QT_MOC_LITERAL(191, 15),  // "processFinished"
        QT_MOC_LITERAL(207, 8),  // "exitCode"
        QT_MOC_LITERAL(216, 20),  // "QProcess::ExitStatus"
        QT_MOC_LITERAL(237, 10),  // "exitStatus"
        QT_MOC_LITERAL(248, 28),  // "on_testConnectionBtn_clicked"
        QT_MOC_LITERAL(277, 20),  // "on_testProcessOutput"
        QT_MOC_LITERAL(298, 22)   // "on_testProcessFinished"
    },
    "MainWindow",
    "on_browseCaCert_clicked",
    "",
    "on_browseClientCert_clicked",
    "on_browseClientKey_clicked",
    "on_connectButton_clicked",
    "on_disconnectButton_clicked",
    "on_refreshInterfaces_clicked",
    "readProcessOutput",
    "processFinished",
    "exitCode",
    "QProcess::ExitStatus",
    "exitStatus",
    "on_testConnectionBtn_clicked",
    "on_testProcessOutput",
    "on_testProcessFinished"
};
#undef QT_MOC_LITERAL
#endif // !QT_MOC_HAS_STRING_DATA
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_CLASSMainWindowENDCLASS[] = {

 // content:
      11,       // revision
       0,       // classname
       0,    0, // classinfo
      11,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   80,    2, 0x08,    1 /* Private */,
       3,    0,   81,    2, 0x08,    2 /* Private */,
       4,    0,   82,    2, 0x08,    3 /* Private */,
       5,    0,   83,    2, 0x08,    4 /* Private */,
       6,    0,   84,    2, 0x08,    5 /* Private */,
       7,    0,   85,    2, 0x08,    6 /* Private */,
       8,    0,   86,    2, 0x08,    7 /* Private */,
       9,    2,   87,    2, 0x08,    8 /* Private */,
      13,    0,   92,    2, 0x08,   11 /* Private */,
      14,    0,   93,    2, 0x08,   12 /* Private */,
      15,    2,   94,    2, 0x08,   13 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 11,   10,   12,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 11,   10,   12,

       0        // eod
};

Q_CONSTINIT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_CLASSMainWindowENDCLASS.offsetsAndSizes,
    qt_meta_data_CLASSMainWindowENDCLASS,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_CLASSMainWindowENDCLASS_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<MainWindow, std::true_type>,
        // method 'on_browseCaCert_clicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'on_browseClientCert_clicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'on_browseClientKey_clicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'on_connectButton_clicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'on_disconnectButton_clicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'on_refreshInterfaces_clicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'readProcessOutput'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'processFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<QProcess::ExitStatus, std::false_type>,
        // method 'on_testConnectionBtn_clicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'on_testProcessOutput'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'on_testProcessFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<QProcess::ExitStatus, std::false_type>
    >,
    nullptr
} };

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MainWindow *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->on_browseCaCert_clicked(); break;
        case 1: _t->on_browseClientCert_clicked(); break;
        case 2: _t->on_browseClientKey_clicked(); break;
        case 3: _t->on_connectButton_clicked(); break;
        case 4: _t->on_disconnectButton_clicked(); break;
        case 5: _t->on_refreshInterfaces_clicked(); break;
        case 6: _t->readProcessOutput(); break;
        case 7: _t->processFinished((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QProcess::ExitStatus>>(_a[2]))); break;
        case 8: _t->on_testConnectionBtn_clicked(); break;
        case 9: _t->on_testProcessOutput(); break;
        case 10: _t->on_testProcessFinished((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QProcess::ExitStatus>>(_a[2]))); break;
        default: ;
        }
    }
}

const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_CLASSMainWindowENDCLASS.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 11;
    }
    return _id;
}
QT_WARNING_POP
