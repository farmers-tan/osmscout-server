#include "requestmapper.h"
#include "dbmaster.h"
#include "geomaster.h"
#include "infohub.h"
#include "config.h"
#include "appsettings.h"

#include "microhttpconnectionstore.h"

#include <microhttpd.h>

#include <QTextStream>
#include <QUrl>
#include <QRunnable>
#include <QThreadPool>
#include <QDir>
#include <QCoreApplication>
#include <QDateTime>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QDebug>

#include <functional>

#define DEFAULT_EXPIRY 3600 // in seconds

//#define DEBUG_CONNECTIONS

RequestMapper::RequestMapper(QObject *parent):
  QObject(parent),
  MicroHTTP::ServiceBase()
{
#ifdef IS_SAILFISH_OS
  // In Sailfish, CPUs could be switched off one by one. As a result,
  // "ideal thread count" set by Qt could be off.
  // In other systems, this procedure is not needed and the defaults can be used
  //
  int cpus = 0;
  QDir dir;
  while ( dir.exists(QString("/sys/devices/system/cpu/cpu") + QString::number(cpus)) )
    ++cpus;

  m_pool.setMaxThreadCount(cpus);

#endif

  InfoHub::logInfo( QCoreApplication::translate("RequestMapper",
                                                "Number of parallel worker threads: %1").arg(m_pool.maxThreadCount()) );

  onSettingsChanged();

  m_info_enable_backends = tr("Enable corresponding backend(s) using Profiles.");

  clock_gettime(CLOCK_BOOTTIME, &m_last_call);

  connect(&m_timer, &QTimer::timeout,
          this, &RequestMapper::checkIdle);
}

RequestMapper::~RequestMapper()
{
}

void RequestMapper::onSettingsChanged()
{
  AppSettings settings;
  m_idle_timeout = settings.valueInt(REQUEST_MAPPER_SETTINGS "idle_timeout");

  // availibility
  m_available_geocodernlp = settings.valueBool(MAPMANAGER_SETTINGS "geocoder_nlp");
  m_available_mapboxgl = settings.valueBool(MAPMANAGER_SETTINGS "mapboxgl");
  m_available_mapnik = settings.valueBool(MAPMANAGER_SETTINGS "mapnik");
  m_available_osmscout = settings.valueBool(MAPMANAGER_SETTINGS "osmscout");
  m_available_valhalla = settings.valueBool(MAPMANAGER_SETTINGS "valhalla");

  if (m_idle_timeout > 0)
    m_timer.start( std::max(1000, (int)m_idle_timeout*1000/10));
  else m_timer.stop();
}

void RequestMapper::checkIdle()
{
  if (m_idle_timeout <= 0) return;

  struct timespec now;
  clock_gettime(CLOCK_BOOTTIME, &now);
  double dt = now.tv_sec - m_last_call.tv_sec;
  if (dt > m_idle_timeout)
    {
      clock_gettime(CLOCK_BOOTTIME, &m_last_call);
      emit idleTimeout();
    }
}

//////////////////////////////////////////////////////////////////////
/// Helper functions to get tile coordinates
//////////////////////////////////////////////////////////////////////

//static int long2tilex(double lon, int z)
//{
//    return (int)(floor((lon + 180.0) / 360.0 * pow(2.0, z)));
//}

//static int lat2tiley(double lat, int z)
//{
//    return (int)(floor((1.0 - log( tan(lat * M_PI/180.0) + 1.0 / cos(lat * M_PI/180.0)) / M_PI) / 2.0 * pow(2.0, z)));
//}

static double tilex2long(int x, int z)
{
  double zs = 1 << z;
  return x / (zs) * 360.0 - 180;
}

static double tiley2lat(int y, int z)
{
  double zs = 1 << z;
  double n = M_PI - 2.0 * M_PI * y / (zs);
  return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

//////////////////////////////////////////////////////////////////////
/// Helper functions to get extract values from query
//////////////////////////////////////////////////////////////////////

template <typename T>
T qstring2value(const QString &, bool &)
{
  // dummy function, implement the specific version for each type separately
  T v;
  return v;
}

template <> int qstring2value(const QString &s, bool &ok)
{
  return s.toInt(&ok);
}

template <> size_t qstring2value(const QString &s, bool &ok)
{
  return s.toUInt(&ok);
}

template <> bool qstring2value(const QString &s, bool &ok)
{
  return (s.toInt(&ok) > 0);
}

template <> double qstring2value(const QString &s, bool &ok)
{
  return s.toDouble(&ok);
}

template <> QString qstring2value(const QString &s, bool &ok)
{
  ok = true;
  return s.trimmed();
}

template <typename T>
T q2value(const QString &key, T default_value, MHD_Connection *q, bool &ok)
{
  const char *vstr = MHD_lookup_connection_value(q, MHD_GET_ARGUMENT_KIND, key.toStdString().c_str());
  if (vstr == NULL)
    return default_value;

  bool this_ok = true;
  T v = qstring2value<T>(vstr,this_ok);
  if (!this_ok)
    v = default_value;
  ok = (ok && this_ok);
  return v;
}

static bool has(const char *key, MHD_Connection *q)
{
  return ( MHD_lookup_connection_value(q, MHD_GET_ARGUMENT_KIND, key)!=nullptr );
}

static bool has(const QString &key, MHD_Connection *q)
{
  return has(key.toStdString().c_str(), q);
}

static int query_uri_iterator(void *cls, enum MHD_ValueKind /*kind*/, const char *key, const char *value)
{
  QString *s = (QString*)cls;
  if ( !s->isEmpty() ) (*s) += "&";
  (*s) += key;
  if (value != NULL)
    {
      (*s) += "=";
      (*s) += value;
    }
  return MHD_YES;
}


//////////////////////////////////////////////////////////////////////
/// Expiry header
//////////////////////////////////////////////////////////////////////
static void set_expiry(MHD_Response *response, unsigned int seconds)
{
  QDateTime dt = QDateTime::currentDateTimeUtc().addSecs(seconds);
  QString expiry = dt.toString("ddd, dd MMM yyyy hh:mm:ss") + " GMT";
  MHD_add_response_header(response, MHD_HTTP_HEADER_EXPIRES, expiry.toStdString().c_str());
}


//////////////////////////////////////////////////////////////////////
/// Default error function
//////////////////////////////////////////////////////////////////////
static void errorText(MHD_Response *response, MicroHTTP::Connection::keytype connection_id, const char *txt, bool verbose=true)
{
  if (verbose)
    InfoHub::logWarning(txt);

  QByteArray data;
  {
    QTextStream output(&data, QIODevice::WriteOnly);
    output << txt;
  }

  MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/html; charset=UTF-8");
  MicroHTTP::ConnectionStore::setData(connection_id, data, false);
}

static void makeEmptyJson(QByteArray &result)
{
  QTextStream output(&result, QIODevice::WriteOnly);
  output << "{ }";
}

void RequestMapper::loguri(const char *uri)
{
  InfoHub::logInfo("Request: " + QString(uri));
}

/////////////////////////////////////////////////////////////////////////////
/// Runnable classes used to solve the tasks
/////////////////////////////////////////////////////////////////////////////

class Task: public QRunnable
{
public:
  Task(MicroHTTP::Connection::keytype key,
       std::function<bool(QByteArray &)> caller,
       QString error_message):
    QRunnable(),
    m_key(key),
    m_caller(caller),
    m_error_message(error_message)
  {
#ifdef DEBUG_CONNECTIONS
    InfoHub::logInfo("Runnable created: " + QString::number((size_t)m_key));
#endif
    InfoHub::addJobToQueue();
  }

  virtual ~Task()
  {
#ifdef DEBUG_CONNECTIONS
    InfoHub::logInfo("Runnable destroyed: " + QString::number((size_t)m_key));
#endif
    InfoHub::removeJobFromQueue();
  }

  virtual void run()
  {
#ifdef DEBUG_CONNECTIONS
    InfoHub::logInfo("Runnable running: " + QString::number((size_t)m_key));
#endif

    QByteArray data;
    if ( !m_caller(data) )
      {
        QByteArray err;
        {
          QTextStream output(&err, QIODevice::WriteOnly);
          output << m_error_message;
        }

#ifdef DEBUG_CONNECTIONS
        InfoHub::logInfo("Runnable submitting error: " + QString::number((size_t)m_key));
#endif
        MicroHTTP::ConnectionStore::setData(m_key, err, false);
        return;
      }

#ifdef DEBUG_CONNECTIONS
    InfoHub::logInfo("Runnable submitting data: " + QString::number((size_t)m_key));
#endif
    MicroHTTP::ConnectionStore::setData(m_key, data, false);
  }

protected:
  MicroHTTP::Connection::keytype m_key;
  std::function<bool(QByteArray &)> m_caller;
  QString m_error_message;
};

/////////////////////////////////////////////////////////////////////////////
/// Request mapper main service function
/////////////////////////////////////////////////////////////////////////////
unsigned int RequestMapper::service(const char *url_c,
                                    MHD_Connection *connection, MHD_Response *response,
                                    MicroHTTP::Connection::keytype connection_id)
{
  QUrl url(url_c);
  QString path(url.path());

  clock_gettime(CLOCK_BOOTTIME, &m_last_call);


  //////////////////////////////////////////////////////////////////////
  /// TILES
  if (path == "/v1/tile")
    {
      if (!m_available_mapnik && !m_available_osmscout)
        {
          errorText(response, connection_id, "Raster tiles are not supported with these settings");
          InfoHub::logWarning(tr("Raster tiles are not available since Mapnik and libosmscout are disabled by selected profile or settings. %1")
                              .arg(m_info_enable_backends));
          return MHD_HTTP_INTERNAL_SERVER_ERROR;
        }

#if !defined(USE_OSMSCOUT) && !defined(USE_MAPNIK)
      errorText(response, connection_id, "Raster tiles are not supported in this configuration");
      return MHD_HTTP_INTERNAL_SERVER_ERROR;
#else
      bool ok = true;
      QString style = q2value<QString>("style", "default", connection, ok);
      bool daylight = q2value<bool>("daylight", true, connection, ok);
      int shift = q2value<int>("shift", 0, connection, ok);
      int scale = q2value<int>("scale", 1, connection, ok);
      int x = q2value<int>("x", 0, connection, ok);
      int y = q2value<int>("y", 0, connection, ok);
      int z = q2value<int>("z", 0, connection, ok);

      if (!ok)
        {
          errorText(response, connection_id, "Error while reading tile query parameters");
          return MHD_HTTP_BAD_REQUEST;
        }

      Task *task = nullptr;

      // ensure that x and y comply with the limits
      int zs = 1 << z;
      if (x > 0) x = x % zs;
      else if (x < 0) x = ( x + (-x/zs + 1)*zs ) % zs;
      if (y > 0) y = y % zs;
      else if (y < 0) y = ( y + (-y/zs + 1)*zs ) % zs;

#ifdef USE_MAPNIK
#ifdef USE_OSMSCOUT
      if ( useMapnik )
#endif
        {
          task = new Task(connection_id,
                          std::bind(&MapnikMaster::renderMap, mapnikMaster,
                                    style, daylight, 256*scale, 256*scale,
                                    tiley2lat(y, z), tilex2long(x, z),
                                    tiley2lat(y+1, z), tilex2long(x+1, z), std::placeholders::_1),
                          "Error while rendering a tile" );
        }
#ifdef USE_OSMSCOUT
      else
#endif
#endif
#ifdef USE_OSMSCOUT
        {
          int ntiles = 1 << shift;
          task = new Task(connection_id,
                          std::bind(&DBMaster::renderMap, osmScoutMaster,
                                    daylight, 96*scale/ntiles, z + shift, 256*scale, 256*scale,
                                    (tiley2lat(y, z) + tiley2lat(y+1, z))/2.0,
                                    (tilex2long(x, z) + tilex2long(x+1, z))/2.0, std::placeholders::_1),
                          "Error while rendering a tile" );
        }
#endif

      m_pool.start(task);

      MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "image/png");
      return MHD_HTTP_OK;

      errorText(response, connection_id, "Error allocating raster tile generation task, possible misconfiguration of the server");
      return MHD_HTTP_INTERNAL_SERVER_ERROR;
#endif
    }

  //////////////////////////////////////////////////////////////////////
  /// MAPBOX GL SUPPORT: CHECK IF IT IS AVAILABLE WHEN REQUESTED
  else if (path.startsWith("/v1/mbgl") && !m_available_mapboxgl)
    {
      errorText(response, connection_id, "Mapbox GL is not supported with these settings");
      InfoHub::logWarning(tr("Mapbox GL backend is disabled by selected profile or settings. %1")
                          .arg(m_info_enable_backends));
      return MHD_HTTP_INTERNAL_SERVER_ERROR;
    }

  //////////////////////////////////////////////////////////////////////
  /// MAPBOX GL SUPPORT: TILES
  else if (path == "/v1/mbgl/tile")
    {
      bool ok = true;
      int x = q2value<int>("x", 0, connection, ok);
      int y = q2value<int>("y", 0, connection, ok);
      int z = q2value<int>("z", 0, connection, ok);

      if (ok && x>=0 && y>=0 && z>=0)
        {
          bool compressed = false;
          bool found = true;
          QByteArray bytes;

          if (!mapboxglMaster->getTile(x, y, z, bytes, compressed, found))
            {
              errorText(response, connection_id, "Error while getting Mapbox GL tile");
              return MHD_HTTP_INTERNAL_SERVER_ERROR;
            }
          if (!found)
            {
              errorText(response, connection_id, "Tile not found", false);
              //return MHD_HTTP_NOT_FOUND;

              // this will force Mapbox GL Native client to load parent tiles. 404 is interpreted
              // as an empty tile by Mapbox GL.
              // See https://github.com/mapbox/mapbox-gl-native/issues/10545
              return 418;
            }

          MicroHTTP::ConnectionStore::setData(connection_id, bytes, false);
          MHD_add_response_header(response, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");
          MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/x-protobuf");
          if (compressed)
            MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_ENCODING, "gzip");
          MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_LENGTH, QString::number(bytes.length()).toStdString().c_str());
          set_expiry(response, DEFAULT_EXPIRY);
          return MHD_HTTP_OK;
        }

      // error condition
      errorText(response, connection_id, "Malformed Mapbox GL tile request");
      return MHD_HTTP_BAD_REQUEST;
    }

  //////////////////////////////////////////////////////////////////////
  /// MAPBOX GL SUPPORT: SPRITE
  else if (path.startsWith("/v1/mbgl/sprite"))
    {
      QByteArray bytes;

      QString fname = path.mid(9); // strlen("/v1/mbgl/"));
      fname.replace("@2x", "");
      if (fname.endsWith(".png"))
        {
          if (!mapboxglMaster->getSpriteImage(fname, bytes))
            {
              errorText(response, connection_id, "Error while getting Mapbox GL sprite image file");
              return MHD_HTTP_NOT_FOUND;
            }

          MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "image/png");
          MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_LENGTH, QString::number(bytes.length()).toStdString().c_str());
        }
      else if (fname.endsWith(".json"))
        {
          if (!mapboxglMaster->getSpriteJson(fname, bytes))
            {
              errorText(response, connection_id, "Error while getting Mapbox GL sprite JSON file");
              return MHD_HTTP_NOT_FOUND;
            }
          MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/json");
          MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_LENGTH, QString::number(bytes.length()).toStdString().c_str());
        }
      else
        {
          errorText(response, connection_id, "Malformed Mapbox GL sprite request");
          return MHD_HTTP_NOT_FOUND;
        }

      MicroHTTP::ConnectionStore::setData(connection_id, bytes, false);
      MHD_add_response_header(response, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");
      set_expiry(response, DEFAULT_EXPIRY);
      return MHD_HTTP_OK;
    }

  //////////////////////////////////////////////////////////////////////
  /// MAPBOX GL SUPPORT: GLYPHS
  else if (path == "/v1/mbgl/glyphs")
    {
      bool ok = true;
      QString stack = q2value<QString>("stack", QString(), connection, ok);
      QString range = q2value<QString>("range", QString(), connection, ok);

      if (ok && !stack.isEmpty() && !range.isEmpty())
        {
          bool compressed = false;
          bool found = true;
          QByteArray bytes;

          if (!mapboxglMaster->getGlyphs(stack, range, bytes, compressed, found))
            {
              errorText(response, connection_id, "Error while getting Mapbox GL glyphs");
              return MHD_HTTP_INTERNAL_SERVER_ERROR;
            }
          if (!found)
            {
              errorText(response, connection_id, "Glyphs not found");
              return MHD_HTTP_NOT_FOUND;
            }

          MicroHTTP::ConnectionStore::setData(connection_id, bytes, false);
          MHD_add_response_header(response, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");
          MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/x-protobuf");
          if (compressed)
            MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_ENCODING, "gzip");
          MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_LENGTH, QString::number(bytes.length()).toStdString().c_str());
          set_expiry(response, DEFAULT_EXPIRY);
          return MHD_HTTP_OK;
        }

      // error condition
      errorText(response, connection_id, "Malformed Mapbox GL glyphs request");
      return MHD_HTTP_BAD_REQUEST;
    }

  //////////////////////////////////////////////////////////////////////
  /// MAPBOX GL SUPPORT: STYLE
  else if (path == "/v1/mbgl/style")
    {
      bool ok = true;
      QString style = q2value<QString>("style", "osmbright", connection, ok);

      if (ok)
        {
          QByteArray bytes;
          if (!mapboxglMaster->getStyle(style, bytes))
            {
              errorText(response, connection_id, "Error while getting Mapbox GL style");
              return MHD_HTTP_NOT_FOUND;
            }

          MicroHTTP::ConnectionStore::setData(connection_id, bytes, false);
          MHD_add_response_header(response, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");
          MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/json");
          MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_LENGTH, QString::number(bytes.length()).toStdString().c_str());
          return MHD_HTTP_OK;
        }

      // error condition
      errorText(response, connection_id, "Malformed Mapbox GL style request");
      return MHD_HTTP_BAD_REQUEST;
    }


  //////////////////////////////////////////////////////////////////////
  /// SEARCH
  else if (path == "/v1/search" || path == "/v2/search")
    {
      if (!m_available_geocodernlp && !m_available_osmscout)
        {
          errorText(response, connection_id, "Search is not supported with these settings");
          InfoHub::logWarning(tr("Search is not available since GeocoderNLP and libosmscout are disabled by selected profile or settings. %1")
                              .arg(m_info_enable_backends));
          return MHD_HTTP_INTERNAL_SERVER_ERROR;
        }

      bool ok = true;
      size_t limit = q2value<size_t>("limit", 25, connection, ok);
      QString search = q2value<QString>("search", "", connection, ok);

      search = search.simplified();

      if (!ok || search.length() < 1)
        {
          errorText(response, connection_id, "Error while reading search query parameters");
          return MHD_HTTP_BAD_REQUEST;
        }

      Task *task;
      bool extended_reply = (path == "/v2/search");

#ifdef USE_OSMSCOUT
      if ( !useGeocoderNLP )
        task = new Task(connection_id,
                        std::bind( &DBMaster::searchExposed, osmScoutMaster,
                                   search, std::placeholders::_1, limit),
                        "Error while searching");
      else
#endif
        task = new Task(connection_id,
                        std::bind( &GeoMaster::searchExposed, geoMaster,
                                   search, std::placeholders::_1, limit,
                                   extended_reply),
                        "Error while searching");

      m_pool.start(task);

      MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/plain; charset=UTF-8");
      return MHD_HTTP_OK;
    }

  //////////////////////////////////////////////////////////////////////
  /// GUIDE: LOOKUP POIs NEAR REFERENCE POINT
  else if (path == "/v1/guide")
    {
      if (!m_available_geocodernlp && !m_available_osmscout)
        {
          errorText(response, connection_id, "Guide is not supported with these settings");
          InfoHub::logWarning(tr("Nearby POI lookup is not available since GeocoderNLP and libosmscout are disabled by selected profile or settings. %1")
                              .arg(m_info_enable_backends));
          return MHD_HTTP_INTERNAL_SERVER_ERROR;
        }

      bool ok = true;
      double radius = q2value<double>("radius", 1000.0, connection, ok);
      size_t limit = q2value<size_t>("limit", 50, connection, ok);
      QString poitype = q2value<QString>("poitype", QString(), connection, ok);
      QString poitype_query = q2value<QString>("query", QString(), connection, ok); // backward compatibility
      QString name_query = q2value<QString>("name", QString(), connection, ok);
      QString search = q2value<QString>("search", QString(), connection, ok);
      double lon = q2value<double>("lng", 0, connection, ok);
      double lat = q2value<double>("lat", 0, connection, ok);

      if (poitype.isEmpty()) poitype = poitype_query;

      if (!ok || (name_query.isEmpty() && poitype.isEmpty()) )
        {
          errorText(response, connection_id, "Error while reading guide query parameters");
          return MHD_HTTP_BAD_REQUEST;
        }

      search = search.simplified();

      if ( has("lng", connection) && has("lat", connection) )
        {
          Task *task;

#ifdef USE_OSMSCOUT
          if ( !useGeocoderNLP )
            task = new Task(connection_id,
                            std::bind(&DBMaster::guide, osmScoutMaster,
                                      poitype, lat, lon, radius, limit, std::placeholders::_1),
                            "Error while looking for POIs in guide");
          else
#endif
            task = new Task(connection_id,
                            std::bind(&GeoMaster::guide, geoMaster,
                                      poitype, name_query, lat, lon, radius, limit, std::placeholders::_1),
                            "Error while looking for POIs in guide");

          m_pool.start(task);
        }

      else if ( has("search", connection) && search.length() > 0 )
        {
          std::string name;

#ifdef USE_OSMSCOUT
          if ( !useGeocoderNLP )
            {
              if (osmScoutMaster->search(search, lat, lon, name))
                {
                  Task *task = new Task(connection_id,
                                        std::bind(&DBMaster::guide, osmScoutMaster,
                                                  poitype, lat, lon, radius, limit, std::placeholders::_1),
                                        "Error while looking for POIs in guide");
                  m_pool.start(task);
                }
              else
                {
                  QByteArray bytes;
                  makeEmptyJson(bytes);
                  MicroHTTP::ConnectionStore::setData(connection_id, bytes, false);
                }
            }
          else
#endif
            {
              if (geoMaster->search(search, lat, lon, name))
                {
                  Task *task = new Task(connection_id,
                                        std::bind(&GeoMaster::guide, geoMaster,
                                                  poitype, name_query, lat, lon, radius, limit, std::placeholders::_1),
                                        "Error while looking for POIs in guide");
                  m_pool.start(task);
                }
              else
                {
                  QByteArray bytes;
                  makeEmptyJson(bytes);
                  MicroHTTP::ConnectionStore::setData(connection_id, bytes, false);
                }
            }
        }

      else
        {
          errorText(response, connection_id, "Error in guide query parameters");
          return MHD_HTTP_BAD_REQUEST;
        }

      MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/plain; charset=UTF-8");
      return MHD_HTTP_OK;
    }

  //////////////////////////////////////////////////////////////////////
  /// LIST AVAILABLE POI TYPES
  else if (path == "/v1/poi_types")
    {

      QByteArray bytes;
#ifdef USE_OSMSCOUT
      if ( !useGeocoderNLP )
        {
          if (!osmScoutMaster->poiTypes(bytes))
            {
              errorText(response, connection_id, "Error while listing available POI types using libosmscout");
              return MHD_HTTP_INTERNAL_SERVER_ERROR;
            }
        }
      else
#endif
        {
          if (!geoMaster->poiTypes(bytes))
            {
              errorText(response, connection_id, "Error while listing available POI types using geocoder-nlp");
              return MHD_HTTP_INTERNAL_SERVER_ERROR;
            }
        }

      MicroHTTP::ConnectionStore::setData(connection_id, bytes, false);
      MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/json; charset=UTF-8");
      return MHD_HTTP_OK;
    }

  //////////////////////////////////////////////////////////////////////
  /// ROUTING VIA OSMSCOUT [V1]
#ifdef USE_OSMSCOUT
  else if (path == "/v1/route" && !useValhalla)
    {
      if (!m_available_osmscout)
        {
          errorText(response, connection_id, "Routing is not supported with these settings");
          InfoHub::logWarning(tr("Routing is not available since Valhalla and libosmscout are disabled by selected profile or settings. %1")
                              .arg(m_info_enable_backends));
          return MHD_HTTP_INTERNAL_SERVER_ERROR;
        }

      bool ok = true;
      QString type = q2value<QString>("type", "car", connection, ok);
      double radius = q2value<double>("radius", 1000.0, connection, ok);
      bool gpx = q2value<int>("gpx", 0, connection, ok);

      std::vector<osmscout::GeoCoord> points;
      std::vector< std::string > names;

      bool points_done = false;
      for (int i=0; !points_done && ok; ++i)
        {
          QString prefix = "p[" + QString::number(i) + "]";
          if ( has(prefix + "[lng]", connection) && has(prefix + "[lat]", connection) )
            {
              double lon = q2value<double>(prefix + "[lng]", 0, connection, ok);
              double lat = q2value<double>(prefix + "[lat]", 0, connection, ok);
              osmscout::GeoCoord c(lat,lon);
              points.push_back(c);
              names.push_back(std::string());
            }

          else if ( has(prefix + "[search]", connection) )
            {
              QString search = q2value<QString>(prefix + "[search]", "", connection, ok);
              search = search.simplified();
              if (search.length()<1)
                {
                  errorText(response, connection_id, "Error in routing parameters: search term is missing" );
                  return MHD_HTTP_BAD_REQUEST;
                }

              double lat, lon;
              std::string name;
              bool unlp = useGeocoderNLP;
              if ( (unlp && geoMaster->search(search, lat, lon, name)) ||
                   (!unlp && osmScoutMaster->search(search, lat, lon, name)) )
                {
                  osmscout::GeoCoord c(lat,lon);
                  points.push_back(c);
                  names.push_back(name);
                }
              else
                ok = false;
            }

          else points_done = true;
        }

      if (!ok || points.size() < 2)
        {
          errorText(response, connection_id, "Error in routing parameters: too few routing points" );
          return MHD_HTTP_BAD_REQUEST;
        }

      osmscout::Vehicle vehicle;
      if (type == "car") vehicle = osmscout::vehicleCar;
      else if (type == "bicycle") vehicle = osmscout::vehicleBicycle;
      else if (type == "foot") vehicle = osmscout::vehicleFoot;
      else
        {
          errorText(response, connection_id, "Error in routing parameters: unknown vehicle" );
          return MHD_HTTP_BAD_REQUEST;
        }

      Task *task = new Task(connection_id,
                            std::bind(&DBMaster::route, osmScoutMaster,
                                      vehicle, points, radius, names, gpx, std::placeholders::_1),
                            "Error while looking for route");
      m_pool.start(task);

      if (!gpx) MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/plain; charset=UTF-8");
      else MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/xml; charset=UTF-8");
      return MHD_HTTP_OK;
    }
#endif

  //////////////////////////////////////////////////////////////////////
  /// ROUTING VIA VALHALLA [V2]
#ifdef USE_VALHALLA
  else if (path == "/v2/route"
         #ifdef USE_OSMSCOUT
           && useValhalla
         #endif
           )
    {
      if (!m_available_valhalla)
        {
          errorText(response, connection_id, "Routing is not supported with these settings");
          InfoHub::logWarning(tr("Routing is not available since Valhalla is disabled by selected profile or settings. %1")
                              .arg(m_info_enable_backends));
          return MHD_HTTP_INTERNAL_SERVER_ERROR;
        }

      QString uri;
      MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND, query_uri_iterator, &uri);

      Task *task = new Task(connection_id,
                            std::bind(&ValhallaMaster::route, valhallaMaster,
                                      uri, std::placeholders::_1),
                            "Error while looking for route via Valhalla");
      m_pool.start(task);

      MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/json; charset=UTF-8");
      return MHD_HTTP_OK;
    }
#endif

  //////////////////////////////////////////////////////////////////////
  /// ROUTING VIA OSMSCOUT [V2]
#ifdef USE_OSMSCOUT
  else if (path == "/v2/route" && !useValhalla)
    {
      if (!m_available_osmscout)
        {
          errorText(response, connection_id, "Routing is not supported with these settings");
          InfoHub::logWarning(tr("Routing is not available since Valhalla and libosmscout are disabled by selected profile or settings. %1")
                              .arg(m_info_enable_backends));
          return MHD_HTTP_INTERNAL_SERVER_ERROR;
        }

      bool ok = true;
      QString json = q2value<QString>("json", "", connection, ok);
      QJsonObject objreq = QJsonDocument::fromJson(json.toUtf8()).object();

      double radius = 1000.0;
      bool gpx = false;

      std::vector<osmscout::GeoCoord> points;
      std::vector< std::string > names;

      osmscout::Vehicle vehicle;
      QString type = objreq.value("costing").toString();
      if (type == "auto") vehicle = osmscout::vehicleCar;
      else if (type == "bicycle") vehicle = osmscout::vehicleBicycle;
      else if (type == "pedestrian") vehicle = osmscout::vehicleFoot;
      else
        {
          errorText(response, connection_id, "Error in routing parameters: unknown vehicle" );
          return MHD_HTTP_BAD_REQUEST;
        }

      QJsonArray arr = objreq.value("locations").toArray();
      for (const auto &a: arr)
        {
          double lat = a.toObject().value("lat").toDouble(-301.0);
          double lon = a.toObject().value("lon").toDouble(-301.0);

          if (lat < -300 || lon < -300)
            {
              errorText(response, connection_id, "Error in routing parameters: unknown coordinates for location" );
              return MHD_HTTP_BAD_REQUEST;
            }

          osmscout::GeoCoord c(lat,lon);
          points.push_back(c);
          names.push_back(std::string());
        }

      if (points.size() < 2)
        {
          errorText(response, connection_id, "Error in routing parameters: too few routing points" );
          return MHD_HTTP_BAD_REQUEST;
        }

      Task *task = new Task(connection_id,
                            std::bind(&DBMaster::route, osmScoutMaster,
                                      vehicle, points, radius, names, gpx, std::placeholders::_1),
                            "Error while looking for route");
      m_pool.start(task);

      MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/plain; charset=UTF-8");
      return MHD_HTTP_OK;
    }
#endif

  // command unidentified. return help string
  errorText(response, connection_id, "Unknown URL path");
  return MHD_HTTP_BAD_REQUEST;
}
