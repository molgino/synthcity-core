// Webserver: Samples/Projekte hoch- und herunterladen, App-Update (siehe web.cpp)
#pragma once
bool webStart(const char* basePath, int port);   // basePath endet mit '/'
int  webPort();                                  // 0 = laeuft nicht
int  webSampleSeq();                             // zaehlt hoch, wenn sich unter samples/ etwas aendert
int  webProjectSeq();                            // ... unter projects/
bool webUpdateReady();                           // Groovebox-OS: Update liegt bereit -> sichern und beenden
int  webLastEvent(char* out, int n);             // laufende Nummer + Text des letzten Uploads
bool webSafeRel(const char* p, int maxDepth);
extern const char WEB_PAGE[];
