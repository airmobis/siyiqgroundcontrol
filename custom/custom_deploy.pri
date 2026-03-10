QMAKE_POST_LINK += && $$QMAKE_COPY $$PWD/deploy/linux/qgroundcontrol-start.sh $$DESTDIR
QMAKE_POST_LINK += && $$QMAKE_COPY $$PWD/deploy/linux/qgroundcontrol.desktop $$DESTDIR
QMAKE_POST_LINK += && $$QMAKE_COPY $$PWD/deploy/linux/qgroundcontrol.png $$DESTDIR
