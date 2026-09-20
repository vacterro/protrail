#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtTest/QTest>
#include <QtGui/QFontDatabase>

class TestBootstrap : public QObject {
    Q_OBJECT
private slots:
    void qtWidgetsLinkWorks();
    void appLifecycle();
};

void TestBootstrap::qtWidgetsLinkWorks() {
    QVERIFY(QApplication::instance() != nullptr);
    QVERIFY(qobject_cast<QApplication*>(QCoreApplication::instance()) != nullptr);
}

void TestBootstrap::appLifecycle() {
    QLabel w;
    w.setWindowTitle("test");
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.close();
    QVERIFY(!w.isVisible());
}

QTEST_MAIN(TestBootstrap)
#include "test_log.moc"
