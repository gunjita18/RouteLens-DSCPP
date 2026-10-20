(function () {
  'use strict';

  var COLORS = ['#0072b2', '#e69f00', '#009e73'];
  var DASH = [null, '10 8', '2 8'];
  var SCENARIO_LABEL = { normal: 'Normal', rush_hour: 'Rush hour', rain: 'Rain', incident: 'Incident' };

  var map = L.map('map').setView([30.325, 78.04], 12);
  L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    attribution: '&copy; OpenStreetMap contributors'
  }).addTo(map);

  var points = [];       // [{lat, lon}]
  var markers = [];
  var routeLayers = [];
  var chart = null;
  var scenario = 'normal';

  var $ = function (id) { return document.getElementById(id); };

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) { e.className = cls; }
    if (text !== undefined) { e.textContent = text; }
    return e;
  }

  function showBanner(msg) {
    var b = $('banner');
    b.textContent = msg || '';
    b.hidden = !msg;
  }

  // ---- point selection -------------------------------------------------
  map.on('click', function (e) {
    if (points.length >= 2) { return; }
    points.push({ lat: e.latlng.lat, lon: e.latlng.lng });
    markers.push(L.marker(e.latlng).bindTooltip(points.length === 1 ? 'Start' : 'End', { permanent: true }).addTo(map));
    updatePointsInfo();
  });

  $('reset').addEventListener('click', function () {
    points = [];
    markers.forEach(function (m) { map.removeLayer(m); });
    markers = [];
    clearRoutes();
    showBanner('');
    updatePointsInfo();
  });

  function updatePointsInfo() {
    var f = function (p) { return p.lat.toFixed(4) + ', ' + p.lon.toFixed(4); };
    var t = 'No points selected.';
    if (points.length === 1) { t = 'Start: ' + f(points[0]) + '. Click the map to set End.'; }
    if (points.length === 2) { t = 'Start: ' + f(points[0]) + ' / End: ' + f(points[1]); }
    $('points-info').textContent = t;
    $('find').disabled = points.length < 2;
  }

  // ---- scenario buttons ------------------------------------------------
  $('scenarios').addEventListener('click', function (e) {
    var btn = e.target.closest('button[data-scenario]');
    if (!btn) { return; }
    scenario = btn.getAttribute('data-scenario');
    Array.prototype.forEach.call($('scenarios').children, function (b) {
      b.setAttribute('aria-checked', b === btn ? 'true' : 'false');
    });
    if (points.length === 2) { findRoutes(); }
  });

  // ---- priority sliders ------------------------------------------------
  ['time', 'distance', 'signals', 'traffic'].forEach(function (k) {
    $('w_' + k).addEventListener('input', function () { $('o_' + k).textContent = $('w_' + k).value; });
    $('w_' + k).addEventListener('change', function () { if (points.length === 2) { findRoutes(); } });
  });

  $('find').addEventListener('click', findRoutes);

  function buildQuery() {
    var q = new URLSearchParams({
      from_lat: points[0].lat, from_lon: points[0].lon,
      to_lat: points[1].lat, to_lon: points[1].lon,
      w_time: $('w_time').value, w_distance: $('w_distance').value,
      w_signals: $('w_signals').value, w_traffic: $('w_traffic').value,
      scenario: scenario
    });
    return q.toString();
  }

  function findRoutes() {
    showBanner('');
    fetch('/api/routes?' + buildQuery())
      .then(function (r) {
        return r.json().then(function (body) { return { status: r.status, body: body }; });
      })
      .then(function (res) {
        if (res.status === 200) {
          render(res.body.routes || []);
        } else if (res.status === 501) {
          clearRoutes();
          showBanner('Routing engine not connected yet. No routes can be computed.');
        } else {
          clearRoutes();
          showBanner('Request rejected: ' + (res.body.error || res.status));
        }
      })
      .catch(function () {
        clearRoutes();
        showBanner('Cannot reach the RouteLens server.');
      });
  }

  // ---- rendering -------------------------------------------------------
  function clearRoutes() {
    routeLayers.forEach(function (l) { map.removeLayer(l); });
    routeLayers = [];
    var cards = $('cards');
    cards.replaceChildren(el('p', 'hint', 'Results appear here.'));
    if (chart) { chart.destroy(); chart = null; }
  }

  function render(routes) {
    clearRoutes();
    if (!routes.length) {
      $('cards').replaceChildren(el('p', 'hint', 'No route found between these points.'));
      return;
    }
    var cards = $('cards');
    cards.replaceChildren();
    var bounds = L.latLngBounds([]);

    routes.forEach(function (r, i) {
      var c = COLORS[i % 3];
      var line = L.polyline(r.polyline, { color: c, weight: r.recommended ? 8 : 5, dashArray: DASH[i % 3], opacity: 0.9 }).addTo(map);
      line.bindPopup(infoBox(r, i));
      routeLayers.push(line);
      bounds.extend(line.getBounds());

      var card = el('div', 'card r' + ((i % 3) + 1));
      var h = el('h3', null, 'Route ' + (i + 1) + (r.name ? ' – ' + r.name : ''));
      if (r.recommended) { h.appendChild(el('span', 'tag', 'Recommended')); }
      card.appendChild(h);
      card.appendChild(stats(r));
      card.addEventListener('click', function () { line.openPopup(); });
      cards.appendChild(card);
    });
    if (bounds.isValid()) { map.fitBounds(bounds, { padding: [30, 30] }); }
    drawChart(routes);
  }

  function stats(r) {
    var dl = el('dl');
    [['ETA', r.eta_min.toFixed(1) + ' min'],
     ['Distance', r.distance_km.toFixed(1) + ' km'],
     ['Signals', String(r.signals)],
     ['Traffic', r.traffic]].forEach(function (p) {
      dl.appendChild(el('dt', null, p[0]));
      dl.appendChild(el('dd', null, p[1]));
    });
    return dl;
  }

  function infoBox(r, i) {
    var box = el('div');
    box.appendChild(el('strong', null, 'Route ' + (i + 1) + (r.recommended ? ' (recommended)' : '')));
    box.appendChild(stats(r));
    return box;
  }

  function drawChart(routes) {
    var labels = routes.map(function (_, i) { return 'Route ' + (i + 1); });
    var series = [
      ['ETA (min)', function (r) { return r.eta_min; }],
      ['Distance (km)', function (r) { return r.distance_km; }],
      ['Signals', function (r) { return r.signals; }]
    ];
    chart = new Chart($('chart'), {
      type: 'bar',
      data: {
        labels: labels,
        datasets: series.map(function (s, k) {
          return { label: s[0], data: routes.map(s[1]), backgroundColor: COLORS[k] };
        })
      },
      options: { responsive: true, maintainAspectRatio: false }
    });
  }

  // ---- traffic status badge --------------------------------------------
  function refreshTraffic() {
    fetch('/api/traffic/status')
      .then(function (r) { return r.json(); })
      .then(function (s) {
        $('traffic-badge').textContent = 'traffic: ' + (s.source === 'live' ? 'live' : 'free-flow fallback');
      })
      .catch(function () { $('traffic-badge').textContent = 'server offline'; });
  }
  refreshTraffic();
  setInterval(refreshTraffic, 30000);
})();
