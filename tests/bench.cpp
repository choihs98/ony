#include <QApplication>
#include <QPainter>
#include <QPen>
#include <QElapsedTimer>
#include <QTest>
#include <QDebug>
#include "MainWindow.h"
#include "CanvasWidget.h"
#include "Document.h"
#include "Style.h"
int main(int argc,char**argv){
  QApplication app(argc,argv); app.setStyle(Style::createStyle()); Style::installMotion();
  QElapsedTimer t; t.start();
  MainWindow w; w.resize(1600,900); w.show(); QTest::qWait(50);
  qInfo()<<"startup ms"<<t.elapsed();
  Document*d=w.document(); d->newProject(QSize(1920,1080),48,24);
  for(int l=0;l<4;++l){ if(l) d->addLayer("L"); QImage im(1920,1080,QImage::Format_ARGB32_Premultiplied); im.fill(Qt::transparent); QPainter p(&im); p.setPen(QPen(Qt::black,6)); for(int i=0;i<40;++i) p.drawLine(i*40,0,1920-i*30,1080); p.end(); for(int f=0;f<48;f+=2) d->pushKeyEdit(l,f,QImage(),im,"x");}
  t.restart(); for(int i=0;i<20;++i){ w.canvas()->grab(); } qInfo()<<"canvas grab ms each"<<t.elapsed()/20.0;
  t.restart(); for(int i=0;i<20;++i){ d->setCurrentFrame(i); d->composite(i,true,false);} qInfo()<<"composite ms each"<<t.elapsed()/20.0;
  t.restart(); for(int i=0;i<10;++i){ w.grab(); } qInfo()<<"window grab ms each"<<t.elapsed()/10.0;
  // stroke
  auto*c=w.canvas(); c->setTool(CanvasWidget::Tool::Pen); BrushParams bp; bp.size=12; c->setBrush(bp);
  t.restart(); QTest::mousePress(c,Qt::LeftButton,{},QPoint(300,300)); for(int i=0;i<60;++i) QTest::mouseMove(c,QPoint(300+i*8,300+(i%7)*9)); QTest::mouseRelease(c,Qt::LeftButton,{},QPoint(800,300));
  qInfo()<<"60-move stroke ms total"<<t.elapsed();
  // 결과 비교용: 여러 브러시로 그리고 키 이미지를 저장
  if (argc>1) {
    d->newProject(QSize(640,400),4,12); c->zoomActual();
    QImage base(640,400,QImage::Format_ARGB32_Premultiplied); base.fill(QColor(80,160,220)); d->pushKeyEdit(0,0,QImage(),base,"b");
    c->setColorA(QColor(20,30,40));
    auto stroke=[&](int preset,int size,bool eraser,bool paper){
      BrushParams p; p.size=size; p.preset=preset; p.opacity=80; c->setBrush(p); c->setPaper(paper);
      c->setTool(eraser?CanvasWidget::Tool::Eraser:CanvasWidget::Tool::Pen);
      static int k=0; ++k; int y=30+k*40;
      QTest::mousePress(c,Qt::LeftButton,{},QPoint(60,y)); for(int i=1;i<40;++i) QTest::mouseMove(c,QPoint(60+i*12,y+int(14*sin(i/3.0)))); QTest::mouseRelease(c,Qt::LeftButton,{},QPoint(540,y));
    };
    stroke(10,30,false,false); stroke(8,40,false,true); stroke(13,50,false,false); stroke(0,14,false,false);
    stroke(11,40,true,false); stroke(19,20,false,false); stroke(22,12,false,false);
    d->keyImage(0,0).save(argv[1]);
  }
}
