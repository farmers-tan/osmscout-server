#include "geomaster.h"
#include "appsettings.h"
#include "config.h"
#include "infohub.h"

#include <QMutexLocker>

#include <QJsonArray>
#include <QJsonDocument>

#include <QDebug>

#include <map>

GeoMaster::GeoMaster(QObject *parent) : QObject(parent)
{
  onSettingsChanged();
}


void GeoMaster::onSettingsChanged()
{
  QMutexLocker lk(&m_mutex);
  AppSettings settings;

  // prepare for new settings
  m_geocoder.drop();
  m_postal.clear_languages();
  m_countries.clear();

  useGeocoderNLP = (settings.valueInt(GEOMASTER_SETTINGS "use_geocoder_nlp") > 0);

  if (!useGeocoderNLP || m_map_selected.isEmpty())
    return; // no need to load anything

  // apply new settings
  m_postal.set_initialize_every_call(settings.valueBool(GEOMASTER_SETTINGS "initialize_every_call"));
  m_postal.set_use_primitive(settings.valueBool(GEOMASTER_SETTINGS "use_primitive"));

  m_search_all_maps = settings.valueBool(GEOMASTER_SETTINGS "search_all_maps");
  m_continue_search_if_hit_found = settings.valueBool(GEOMASTER_SETTINGS "continue_search_if_hit_found");

  // fill country list
  {
    m_postal_full_library = (m_postal_global.isEmpty() && m_postal_country_dirs.isEmpty());

    // put selected map as a first element in the countries list
    if (m_geocoder_dirs.contains(m_map_selected) &&
        (m_postal_full_library || m_postal_country_dirs.contains(m_map_selected)))
      m_countries.append(m_map_selected);
    else
      {
        InfoHub::logError(tr("Selected country not available for geocoder: %1").arg(m_map_selected));
        return;
      }

    if (m_search_all_maps)
      {
        QStringList geokeys = m_geocoder_dirs.keys();
        geokeys.removeAll(m_map_selected);
        for (const QString &k: geokeys)
          if (m_postal_full_library || m_postal_country_dirs.contains(k))
            m_countries.append(k);
      }

    QString info = tr("Countries used in search: ");
    for (const QString &k: m_countries)
      info += k + " ";
    InfoHub::logInfo(info);
  }

  // prepare postal and geocoder
  std::string postal_global = m_postal_global.toStdString();
  std::string postal_country = m_postal_country_dirs.value(m_map_selected).toStdString();
  m_postal.set_postal_datadir(postal_global, postal_country);

  QString geopath = m_geocoder_dirs.value(m_map_selected);
  if (geopath.length() < 1 || !m_geocoder.load(geopath.toStdString()))
    {
      InfoHub::logError(tr("Cannot open geocoder database") + ": " + geopath);
      return;
    }

  InfoHub::logInfo(tr("Opened geocoder database") + " " + geopath, true);

  m_geocoder.set_max_queries_per_hierarchy(settings.valueInt(GEOMASTER_SETTINGS "max_queries_per_hierarchy"));

  QString lang = settings.valueString(GEOMASTER_SETTINGS "languages");

  if (lang.length() > 0)
    {
      QStringList lngs = lang.split(',', QString::SkipEmptyParts);
      QString used;
      for (QString l: lngs)
        {
          l = l.simplified();
          m_postal.add_language(l.toStdString());
          used += l + " ";
        }
      InfoHub::logInfo(tr("libpostal using languages: %1").arg(used));
    }
  else
    InfoHub::logInfo(tr("libpostal will use all covered languages"));
}

void GeoMaster::onGeocoderNLPChanged(QHash<QString, QString> dirs)
{
  bool changed;
  {
    QMutexLocker lk(&m_mutex);
    changed = (m_geocoder_dirs != dirs);
    m_geocoder_dirs = dirs;
  }
  if (changed)
    onSettingsChanged();
}

void GeoMaster::onPostalChanged(QString global, QHash<QString, QString> country_dirs)
{
  bool changed;
  {
    QMutexLocker lk(&m_mutex);
    changed = (m_postal_global != global || m_postal_country_dirs != country_dirs);
    m_postal_global = global;
    m_postal_country_dirs = country_dirs;
  }
  if (changed)
    onSettingsChanged();
}

void GeoMaster::onSelectedMapChanged(QString selected)
{
  bool changed;
  {
    QMutexLocker lk(&m_mutex);
    changed = (m_map_selected != selected);
    m_map_selected = selected;
  }
  if (changed)
    onSettingsChanged();
}

static std::string v2s(const std::vector<std::string> &v)
{
  std::string s = "{";
  for (auto i: v)
    {
      if (s.length() > 1) s += ", ";
      s += i;
    }
  s += "}";
  return s;
}

bool GeoMaster::search(const QString &searchPattern, QJsonObject &result, size_t limit,
                       double &lat, double &lon, std::string &name, size_t &number_of_results)
{
  QMutexLocker lk(&m_mutex);

  std::vector<GeoNLP::Geocoder::GeoResult> search_result;
  QJsonObject parsed;
  QJsonObject parsed_normalized;

  struct PostalRes {
    std::vector< GeoNLP::Postal::ParseResult > parsed;
    GeoNLP::Postal::ParseResult nonorm;
  };

  size_t levels_resolved = 0;
  std::map< std::string, PostalRes > postal_cache;
  for(const QString country: m_countries)
    {
      if (!m_geocoder.load(m_geocoder_dirs.value(country).toStdString()))
        {
          InfoHub::logError(tr("Cannot open geocoding database: %1").arg(m_geocoder_dirs.value(country)));
          return false;
        }

      // parsing with libpostal
      std::vector< GeoNLP::Postal::ParseResult > parsed_query;
      GeoNLP::Postal::ParseResult nonorm;
      std::string postal_id;

      if (!m_postal_full_library)
        postal_id = m_postal_country_dirs.value(country).toStdString();

      if ( postal_cache.count(postal_id) > 0 )
        {
          PostalRes &r = postal_cache[postal_id];
          parsed_query = r.parsed;
          nonorm = r.nonorm;
        }
      else
        {
          if (!m_postal_full_library)
            m_postal.set_postal_datadir_country(postal_id);

          if ( !m_postal.parse( searchPattern.toStdString(),
                                parsed_query, nonorm) )
            {
              InfoHub::logError(tr("Error parsing by libpostal, maybe libpostal databases are not available"));
              return false;
            }

          // record parsing results
          {
            QJsonObject r;
            for (auto a: nonorm)
              r.insert(QString::fromStdString(a.first), QString::fromStdString(v2s(a.second)));
            parsed.insert(country, r);
          }

          PostalRes r;
          r.parsed = parsed_query;
          r.nonorm = nonorm;
          postal_cache[postal_id] = r;

          {
            QJsonArray arr;
            QStringList info_id_split = QString::fromStdString(postal_id).split('/');
            QString info_id;
            if (info_id_split.size() > 0)
              info_id = info_id_split.value(info_id_split.size()-1);

            for (const GeoNLP::Postal::ParseResult &pr: parsed_query)
              {
                QJsonObject r;
                QString info;
                for (auto a: pr)
                  {
                    r.insert(QString::fromStdString(a.first), QString::fromStdString(v2s(a.second)));
                    info += QString::fromStdString(a.first) + ": " + QString::fromStdString(v2s(a.second)) + "; ";
                  }

                arr.push_back(r);

                InfoHub::logInfo(tr("Parsed query [%1]: %2").arg(info_id).arg(info));
              }
            parsed_normalized.insert(country, arr);
          }
        }

      // search
      m_geocoder.set_max_results(limit);
      std::vector<GeoNLP::Geocoder::GeoResult> search_result_country;
      if ( !m_geocoder.search(parsed_query, search_result_country, levels_resolved) )
        {
          InfoHub::logError(tr("Error while searching with geocoder-nlp"));
          return false;
        }

      if (!search_result_country.empty())
        {
          if ( search_result.empty() ||
               ( search_result[0].levels_resolved < search_result_country[0].levels_resolved ) )
            {
              search_result = search_result_country;
              levels_resolved = search_result_country[0].levels_resolved;
            }
          else if ( search_result[0].levels_resolved == search_result_country[0].levels_resolved )
            search_result.insert(search_result.end(),
                                 search_result_country.begin(), search_result_country.end());
        }

      if (!search_result.empty() && !m_continue_search_if_hit_found)
        break;
    }

  // enforce the limit
  if (search_result.size() > limit)
    search_result.resize(limit);

  // record results
  result.insert("query", searchPattern);
  result.insert("parsed", parsed);
  result.insert("parsed_normalized", parsed_normalized);

  {
    QJsonArray arr;
    for (const GeoNLP::Geocoder::GeoResult &sr: search_result)
      {
        QJsonObject r;

        r.insert("admin_region", QString::fromStdString(sr.address));
        r.insert("title", QString::fromStdString(sr.title));
        r.insert("lat", sr.latitude);
        r.insert("lng", sr.longitude);
        r.insert("object_id", sr.id);
        r.insert("type", QString::fromStdString(sr.type));
        r.insert("levels_resolved", (int)sr.levels_resolved);

        arr.push_back(r);
      }

    result.insert("result", arr);
  }

  number_of_results = search_result.size();
  if (number_of_results > 0)
    {
      lat = search_result[0].latitude;
      lon = search_result[0].longitude;
      name = search_result[0].address;
    }

  return true;
}

bool GeoMaster::search(const QString &searchPattern, double &lat, double &lon, std::string &name)
{
  QJsonObject obj;
  size_t number_of_results;

  if ( !search(searchPattern, obj, 1, lat, lon, name, number_of_results ) )
    {
      InfoHub::logWarning("Search for reference point failed");
      return false;
    }

  if ( number_of_results > 0 )
    return true;

  InfoHub::logWarning(tr("Search for reference point failed: cannot find") + " " + searchPattern);
  return false;
}

bool GeoMaster::searchExposed(const QString &searchPattern, QByteArray &result, size_t limit, bool full_result)
{
  QJsonObject sres;
  double lat, lon;
  std::string name;
  size_t number_of_results;

  if ( !search(searchPattern, sres, limit, lat, lon, name, number_of_results ) )
    return false;

  if (!full_result)
    {
      QJsonDocument document(sres.value("result").toArray());
      result = document.toJson();
    }
  else
    {
      QJsonDocument document(sres);
      result = document.toJson();
    }

  return true;
}
