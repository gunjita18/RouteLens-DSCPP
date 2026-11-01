(function () {
  'use strict';

  var COLORS = ['#0072b2', '#e69f00', '#009e73'];
  var CHART_COLORS = ['#0072b2', '#56b4e9', '#e69f00', '#cc79a7'];  // Okabe-Ito
  var DASH = [null, '10 8', '2 8'];
  var SCENARIO_LABEL = { normal: 'Normal', rush_hour: 'Rush hour', rain: 'Rain', incident: 'Incident' };
  var CLOSURE_STYLE = {
    block: { color: '#d55e00', dashArray: null, label: 'Closed' },
    slow: { color: '#cc79a7', dashArray: '6 6', label: 'Slow' }
  };
  var MAX_CLOSURES = 20;   // server limit for POST /api/scenario

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
  var closures = [];     // [{lat, lon, mode, radius_m, slow_factor, layer}]
  var placing = false;   // true: map clicks add closures instead of Start/End
  var closureMode = 'block';

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
    if (placing) { addClosure(e.latlng); return; }
    if (points.length >= 2) { return; }
    points.push({ lat: e.latlng.lat, lon: e.latlng.lng });
    var idx = points.length - 1;
    var marker = L.marker(e.latlng, { draggable: true, title: 'Drag to move' })
      .bindTooltip(idx === 0 ? 'Start' : 'End', { permanent: true }).addTo(map);
    // Dragging a marker moves that point and, once both are set, recomputes the routes.
    marker.on('dragend', function () {
      var ll = marker.getLatLng();
      points[idx] = { lat: ll.lat, lon: ll.lng };
      updatePointsInfo();
      refresh();
    });
    markers.push(marker);
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

  // ---- what-if closures ------------------------------------------------
  // With at least one closure every route request goes to POST /api/scenario,
  // without any it goes to /api/routes, so the page never mixes the two.
  $('place-closures').addEventListener('click', function () {
    placing = !placing;
    var b = $('place-closures');
    b.setAttribute('aria-pressed', placing ? 'true' : 'false');
    b.textContent = placing ? 'Placing closures: click the map (click here to stop)' : 'Place closures on the map';
    $('map').classList.toggle('placing', placing);
  });

  $('closure-mode').addEventListener('click', function (e) {
    var btn = e.target.closest('button[data-mode]');
    if (!btn) { return; }
    closureMode = btn.getAttribute('data-mode');
    Array.prototype.forEach.call($('closure-mode').children, function (b) {
      b.setAttribute('aria-checked', b === btn ? 'true' : 'false');
    });
    $('slow-label').hidden = closureMode !== 'slow';
  });

  $('radius').addEventListener('input', function () { $('o_radius').textContent = $('radius').value + ' m'; });

  $('clear-closures').addEventListener('click', function () {
    closures.forEach(function (c) { map.removeLayer(c.layer); });
    closures = [];
    updateClosures();
    refresh();
  });

  function closureText(c, i) {
    var t = CLOSURE_STYLE[c.mode].label + ' ' + (i + 1);
    var detail = c.mode === 'slow' ? c.slow_factor + ' × longer, ' : 'blocked, ';
    return t + ' (' + detail + c.radius_m + ' m)';
  }

  function addClosure(latlng) {
    if (closures.length >= MAX_CLOSURES) {
      showBanner('At most ' + MAX_CLOSURES + ' closures. Remove one to add another.');
      return;
    }
    var c = {
      lat: latlng.lat, lon: latlng.lng, mode: closureMode,
      radius_m: Number($('radius').value),
      slow_factor: Number($('slow_factor').value)
    };
    var style = CLOSURE_STYLE[c.mode];
    // Not interactive, so a click on an existing closure still reaches the map.
    c.layer = L.circle(latlng, {
      radius: c.radius_m, color: style.color, weight: 3, dashArray: style.dashArray,
      fillColor: style.color, fillOpacity: 0.15, interactive: false
    }).addTo(map);
    closures.push(c);
    updateClosures();
    refresh();
  }

  function removeClosure(i) {
    map.removeLayer(closures[i].layer);
    closures.splice(i, 1);
    updateClosures();
    refresh();
  }

  // Renumbers the list and the map labels, and updates the Find button.
  function updateClosures() {
    var list = $('closure-list');
    list.replaceChildren();
    closures.forEach(function (c, i) {
      c.layer.unbindTooltip();
      c.layer.bindTooltip(CLOSURE_STYLE[c.mode].label + ' ' + (i + 1), { permanent: true, direction: 'center' });
      var li = el('li');
      li.appendChild(el('span', null, closureText(c, i)));
      var rm = el('button', null, 'Remove');
      rm.type = 'button';
      rm.setAttribute('aria-label', 'Remove ' + closureText(c, i));
      rm.addEventListener('click', function () { removeClosure(i); });
      li.appendChild(rm);
      list.appendChild(li);
    });
    $('clear-closures').hidden = closures.length === 0;
    setBusy(false);
  }

  // Recomputes whatever is on screen after any input changes.
  function refresh() {
    if (points.length < 2) { return; }
    if (closures.length) { runWhatIf(); } else { findRoutes(); }
  }

  // ---- scenario buttons ------------------------------------------------
  $('scenarios').addEventListener('click', function (e) {
    var btn = e.target.closest('button[data-scenario]');
    if (!btn) { return; }
    scenario = btn.getAttribute('data-scenario');
    Array.prototype.forEach.call($('scenarios').children, function (b) {
      b.setAttribute('aria-checked', b === btn ? 'true' : 'false');
    });
    // An incident is a road the user blocks or slows, so start placing closures.
    if (scenario === 'incident' && !placing) { $('place-closures').click(); }
    refresh();
  });

  // ---- priority sliders ------------------------------------------------
  ['time', 'distance', 'junctions', 'traffic'].forEach(function (k) {
    $('w_' + k).addEventListener('input', function () { $('o_' + k).textContent = $('w_' + k).value; });
    $('w_' + k).addEventListener('change', refresh);
  });

  $('find').addEventListener('click', refresh);

  function buildQuery() {
    var q = new URLSearchParams({
      from_lat: points[0].lat, from_lon: points[0].lon,
      to_lat: points[1].lat, to_lon: points[1].lon,
      w_time: $('w_time').value, w_distance: $('w_distance').value,
      w_junctions: $('w_junctions').value, w_traffic: $('w_traffic').value,
      scenario: scenario
    });
    return q.toString();
  }

  // Shows or hides the permanent warning used when the server returns sample routes.
  function setPreview(on) { $('preview-banner').hidden = !on; }

  function setBusy(busy) {
    var b = $('find');
    var what = closures.length ? 'Find routes with ' + closures.length + (closures.length === 1 ? ' closure' : ' closures') : 'Find routes';
    b.textContent = busy ? what.replace('Find', 'Finding') + '…' : what;
    b.disabled = busy || points.length < 2;
    $('cards').setAttribute('aria-busy', busy ? 'true' : 'false');
  }

  var requestSeq = 0;   // only the newest request may update the page

  function findRoutes() {
    send(fetch('/api/routes?' + buildQuery()), function (body) {
      var note = scenario === 'incident'
        ? 'Incident: click the road where it happened (section 3, Block or Slow). Until then, normal conditions.'
        : 'Scenario: ' + (SCENARIO_LABEL[scenario] || scenario) + '. The recommended route follows your priority sliders.';
      render(body.routes || [], note);
      drawSnapped(body.snapped);
      setPreview(body.preview === true);
    });
  }

  function runWhatIf() {
    var body = {
      from_lat: points[0].lat, from_lon: points[0].lon,
      to_lat: points[1].lat, to_lon: points[1].lon,
      scenario: scenario,   // rain and rush hour speeds apply on top of the closures
      closures: closures.map(function (c) {
        var out = { lat: c.lat, lon: c.lon, radius_m: c.radius_m, mode: c.mode };
        if (c.mode === 'slow') { out.slow_factor = c.slow_factor; }
        return out;
      })
    };
    ['time', 'distance', 'junctions', 'traffic'].forEach(function (k) {
      body['w_' + k] = Number($('w_' + k).value);
    });
    var request = fetch('/api/scenario', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    send(request, function (res) {
      var n = closures.length;
      var speeds = scenario === 'rain' || scenario === 'rush_hour' ? SCENARIO_LABEL[scenario] + ' speeds' : 'free-flow speeds';
      render(res.routes || [], 'What-if: ' + n + (n === 1 ? ' closure' : ' closures') +
        ' applied, ' + speeds + '. The recommended route follows your priority sliders.',
        res.message ? 'No route avoids these closures.' : undefined);   // the reason is in the impact box
      drawSnapped(res.snapped);
      showImpact(res);
    });
  }

  // Sends one route request; only the newest one may update the page. onOk gets
  // the body of a 200 answer; every other status is handled here the same way.
  function send(request, onOk) {
    showBanner('');
    var mySeq = ++requestSeq;
    setBusy(true);
    request
      .then(function (r) {
        return r.json().then(function (body) { return { status: r.status, body: body }; });
      })
      .then(function (res) {
        if (mySeq !== requestSeq) { return; }   // a newer request is already running
        if (res.status === 200) {
          onOk(res.body);
        } else if (res.status === 429) {
          clearRoutes();
          showBanner('Too many requests. Wait a second and try again.');
        } else if (res.status === 501) {
          clearRoutes();
          drawSnapped(res.body.snapped);
          showBanner('Routing engine not connected yet. No routes can be computed.');
        } else {
          clearRoutes();
          showBanner('Request rejected: ' + (res.body.error || res.status));
        }
      })
      .catch(function () {
        if (mySeq !== requestSeq) { return; }
        clearRoutes();
        showBanner('Cannot reach the RouteLens server.');
      })
      .then(function () {
        if (mySeq === requestSeq) { setBusy(false); }   // always runs, success or failure
      });
  }

  // ---- rendering -------------------------------------------------------
  // Shows where each click landed on the road network (the junction the
  // server snapped it to). Kept in routeLayers so clearRoutes() removes them.
  function drawSnapped(snapped) {
    if (!snapped) { return; }
    [['from', 'Start'], ['to', 'End']].forEach(function (k) {
      var s = snapped[k[0]];
      if (!s) { return; }
      var label = k[1] + ' snapped to a ' + s.road_class + ' road (' + Math.round(s.road_distance_m) + ' m from your click)';
      var m = L.circleMarker([s.lat, s.lon], { radius: 7, color: '#1c2430', weight: 2, fillColor: '#ffffff', fillOpacity: 1 })
        .bindTooltip(el('span', null, label)).addTo(map);
      routeLayers.push(m);
    });
  }

  function clearRoutes() {
    routeLayers.forEach(function (l) { map.removeLayer(l); });
    routeLayers = [];
    var cards = $('cards');
    cards.replaceChildren(el('p', 'hint', 'Results appear here.'));
    if (chart) { chart.destroy(); chart = null; }
    $('comparison-section').hidden = true;   // no empty "Comparison" heading
    $('scenario-note').hidden = true;
    $('whatif-result').hidden = true;
  }

  function render(routes, noteText, emptyMessage) {
    clearRoutes();
    if (!routes.length) {
      $('cards').replaceChildren(el('p', 'hint', emptyMessage || 'No route found between these points.'));
      return;
    }
    var cards = $('cards');
    cards.replaceChildren();
    var note = $('scenario-note');
    note.textContent = noteText;
    note.hidden = false;
    var bounds = L.latLngBounds([]);

    routes.forEach(function (r, i) {
      var c = COLORS[i % 3];
      var line = L.polyline(r.polyline, {
        color: c, weight: r.recommended ? 8 : 5, dashArray: DASH[i % 3], opacity: 0.9,
        bubblingMouseEvents: false   // the map's own click handler must not run too
      }).addTo(map);
      // Not bindPopup(): its click handler stops the event, so a click on a
      // route could never place a closure, and routes are where closures go.
      var popup = L.popup().setContent(infoBox(r, i));
      line.on('click', function (e) {
        if (placing) { addClosure(e.latlng); } else { popup.setLatLng(e.latlng).openOn(map); }
      });
      routeLayers.push(line);
      bounds.extend(line.getBounds());

      var card = el('div', 'card r' + ((i % 3) + 1));
      var h = el('h3', null, 'Route ' + (i + 1) + (r.name ? ' – ' + r.name : ''));
      if (r.recommended) { h.appendChild(el('span', 'tag', 'Recommended')); }
      card.appendChild(h);
      card.appendChild(stats(r));
      if (r.explanation) { card.appendChild(el('p', 'explain', r.explanation)); }
      card.addEventListener('click', function () { popup.setLatLng(line.getCenter()).openOn(map); });
      cards.appendChild(card);
    });
    if (bounds.isValid()) { map.fitBounds(bounds, { padding: [30, 30] }); }
    drawChart(routes);
  }

  // Seconds -> "37 min" / "1 h 5 min". Rounding happens here, at display time.
  function formatDuration(seconds) {
    var total = Math.round(seconds / 60);
    if (total < 1) { return '< 1 min'; }
    if (total < 60) { return total + ' min'; }
    var h = Math.floor(total / 60);
    var m = total % 60;
    return m === 0 ? h + ' h' : h + ' h ' + m + ' min';
  }

  function pct(v) { return Math.round(v) + '%'; }

  // Summary of a what-if answer: reachability, how the fastest route changed,
  // how many roads were hit, and closures that touched no road at all.
  function showImpact(res) {
    var box = $('whatif-result');
    box.replaceChildren();
    var add = function (text, cls) { box.appendChild(el('p', cls, text)); };

    if (res.reachable && res.reachable.with_closures === false) {
      add('The closures cut off every road to the destination. Remove or move one.', 'warn');
    } else if (res.impact && Array.isArray(res.routes) && res.routes.length) {
      var fastestNow = Math.min.apply(null, res.routes.map(function (r) { return r.eta_s; }));
      var delta = res.impact.fastest_eta_delta_s;
      // Compare whole minutes, so "44 min (was 39 min, 5 min longer)" adds up.
      var longerMin = Math.round(fastestNow / 60) - Math.round((fastestNow - delta) / 60);
      if (delta <= -30) {   // smaller differences are near-duplicate routes, not news
        // Closing roads cannot make a trip faster; a faster route than any shown before was found.
        add('Fastest route: ' + formatDuration(fastestNow) + '. A faster route than any shown without closures turned up, so compare the routes rather than the totals.');
      } else if (longerMin < 1) {
        add('Fastest route: ' + formatDuration(fastestNow) + ', practically unchanged by the closures.');
      } else {
        add('Fastest route: ' + formatDuration(fastestNow) + ' (was ' +
            formatDuration(fastestNow - delta) + ', ' + formatDuration(longerMin * 60) + ' longer).');
      }
    }

    if (res.affected) {
      var parts = [];
      if (res.affected.blocked_edges) { parts.push(res.affected.blocked_edges + ' blocked'); }
      if (res.affected.slowed_edges) { parts.push(res.affected.slowed_edges + ' slowed'); }
      add(parts.length ? 'Road segments affected: ' + parts.join(', ') + '.' : 'No road segment is affected.');
    }

    (res.closures || []).forEach(function (c, i) {
      if (c.edges_affected === 0) {
        add(CLOSURE_STYLE[c.mode].label + ' ' + (i + 1) + ' touches no road. Move it onto a road or make it larger.', 'warn');
      }
    });
    box.hidden = box.childNodes.length === 0;
  }

  function stats(r) {
    var rows = [['ETA (with traffic)', formatDuration(r.eta_s)]];
    if (typeof r.free_flow_eta_s === 'number' && r.free_flow_eta_s > 0) {
      rows.push(['ETA (no traffic)', formatDuration(r.free_flow_eta_s)]);
    }
    rows.push(['Distance', r.distance_km.toFixed(1) + ' km'],
              ['Major junctions', String(r.junctions)],
              ['Traffic', r.traffic]);
    if (r.coverage) {
      rows.push(['Data', 'live ' + pct(r.coverage.live_pct) +
                         ', typical ' + pct(r.coverage.typical_pct) +
                         ', estimated ' + pct(r.coverage.estimated_pct)]);
    }
    var dl = el('dl');
    rows.forEach(function (p) {
      dl.appendChild(el('dt', null, p[0]));
      dl.appendChild(el('dd', null, p[1]));
    });
    return dl;
  }

  function infoBox(r, i) {
    var box = el('div');
    box.appendChild(el('strong', null, 'Route ' + (i + 1) + (r.recommended ? ' (recommended)' : '')));
    box.appendChild(stats(r));
    if (r.explanation) { box.appendChild(el('p', 'explain', r.explanation)); }
    return box;
  }

  function drawChart(routes) {
    var labels = routes.map(function (_, i) { return 'Route ' + (i + 1); });
    var series = [
      ['ETA with traffic (min)', function (r) { return r.eta_s / 60; }],
      ['ETA no traffic (min)', function (r) { return (r.free_flow_eta_s || 0) / 60; }],
      ['Distance (km)', function (r) { return r.distance_km; }],
      ['Major junctions', function (r) { return r.junctions; }]
    ];
    $('comparison-section').hidden = false;   // visible before Chart.js measures the canvas
    chart = new Chart($('chart'), {
      type: 'bar',
      data: {
        labels: labels,
        datasets: series.map(function (s, k) {
          return { label: s[0], data: routes.map(s[1]), backgroundColor: CHART_COLORS[k] };
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

  // If the server runs in preview mode, warn from the very first screen.
  fetch('/api/health')
    .then(function (r) { return r.json(); })
    .then(function (h) { setPreview(h.preview === true); })
    .catch(function () { /* the traffic badge already reports an offline server */ });
})();
