// https://www.heavy.ai/blog/12-color-palettes-for-telling-better-stories-with-your-data
const colours = ["#ea5545", "#f46a9b", "#ef9b20", "#edbf33", "#ede15b", "#bdcf32", "#87bc45", "#27aeef", "#b33dc6"];

function load_timings(filename)
{
	const xhr = new XMLHttpRequest();
	xhr.open('GET', 'timings.csv', true);

	xhr.onreadystatechange = () => {
		if (xhr.readyState === XMLHttpRequest.DONE)
		{
			const status = xhr.status;
			if (status === 0 || (status >= 200 && status < 400))
			{
				const text = xhr.responseText;
				lines = text.split('\n');

				let timing_data = [];

				for(let i in lines)
				{
					items = lines[i].split(',');
					if (items.length < 4)
						continue;

					event_name = items[0].replaceAll('"', '');
					frameindex = parseInt(items[1]);
					timestamp = parseInt(items[2]);


					if (items[3] == "255")
					{
						streamindex = "global";
					}
					else
					{
						streamindex = "stream-" + items[3];
					}

					if (!(frameindex in timing_data))
					{
						timing_data[frameindex] = new Object();
					}

					if (!(streamindex in timing_data[frameindex]))
					{
						timing_data[frameindex][streamindex] = new Object();
					}

					timing_data[frameindex][streamindex][event_name] = timestamp;
				}

				let min_timestamp = Number.MAX_VALUE;
				for(let i in timing_data)
				{
					for(let j in timing_data[i])
					{
						for(let k in timing_data[i][j])
						{
							min_timestamp = Math.min(min_timestamp, timing_data[i][j][k]);
						}
					}
				}

				for(let i in timing_data)
				{
					var frame_begin = Number.MAX_VALUE;
					var frame_end = -Number.MAX_VALUE;
					for(let j in timing_data[i])
					{
						for(let k in timing_data[i][j])
						{
							frame_begin = Math.min(frame_begin, timing_data[i][j][k]);
							frame_end = Math.max(frame_end, timing_data[i][j][k]);
						}
					}

					for(let j in timing_data[i])
					{
						for(let k in timing_data[i][j])
						{
							timing_data[i][j][k] = (timing_data[i][j][k] - frame_begin) / 1000000; // nanoseconds to milliseconds
						}
					}

					if (timing_data[i].global)
					{
						timing_data[i].global.frame_begin = (frame_begin - min_timestamp) / 1000000;
						timing_data[i].global.frame_end = (frame_end - min_timestamp) / 1000000;
					}
				}

				document.getElementById('debug').innerHTML = JSON.stringify(timing_data, null, 4);

				draw_everything(document.getElementById('timeline'), timing_data);
			}
		}
	};

	xhr.send(null);
}

function draw_timescale(svg, t0, t1, dt, t_offset, t_scale, height)
{
	for(let t = t0; t < t1; t = t + dt)
	{
		let line = document.createElementNS("http://www.w3.org/2000/svg", 'line');
		const x = (t - t_offset) * t_scale;
		line.setAttribute('x1', x);
		line.setAttribute('y1', 0);
		line.setAttribute('x2', x);
		line.setAttribute('y2', height);
		line.setAttribute('stroke', '#808080');
		svg.appendChild(line);

		let text = document.createElementNS("http://www.w3.org/2000/svg", 'text');
		text.setAttribute('x', x);
		text.setAttribute('y', height);
		text.setAttribute('dominant-baseline', 'hanging');
		text.setAttribute('text-anchor', 'left');
		// text.setAttribute('stroke', '#808080');
		text.innerHTML = t + ' ms';
		svg.appendChild(text);
	}
}

function draw_frame(svg, frame_id, frame_data, t_offset, t_scale, line_height, line_margin)
{
	let g = document.createElementNS("http://www.w3.org/2000/svg", 'g');

	if (!("global" in frame_data))
		return;

	if (!("wake_up" in frame_data["global"]) || !("submit" in frame_data["global"]))
		return;

	const t0 = frame_data.global.frame_begin;
	g.frame_begin = frame_data.global.frame_begin;
	g.setAttribute("transform", "translate(" + ((t0-t_offset) * t_scale) + " 0)");
	g.setAttribute("id", frame_id);
	g.setAttribute("class", 'frame');

	// Global stuff
	const wake_up = frame_data["global"]["wake_up"];
	const submit = frame_data["global"]["submit"];

	let compositor = document.createElementNS("http://www.w3.org/2000/svg", 'rect');
	compositor.setAttribute('x', wake_up * t_scale);
	compositor.setAttribute('y', 0);
	compositor.setAttribute('width', (submit - wake_up) * t_scale);
	compositor.setAttribute('height', line_height);
	compositor.setAttribute('fill', colours[0]);
	compositor.setAttribute('class', 'compositor');
	g.appendChild(compositor);

	var text = document.createElementNS("http://www.w3.org/2000/svg", 'text');
	text.setAttribute('x', wake_up * t_scale);
	text.setAttribute('y', line_height / 2);
	text.setAttribute('dominant-baseline', 'middle');
	text.setAttribute('text-anchor', 'left');
	text.innerHTML = 'Frame ' + frame_id.replace("frame-", "");
	g.appendChild(text);

	// Per-encoder stuff
	const dy = line_height + line_margin;
	for(let stream in frame_data)
	{
		if (stream === "global")
			continue;

		const stream_nr = parseInt(stream.replace("stream-", ""));
		let y0 = dy * (1 + 4 * stream_nr);

		const events = frame_data[stream];

		var encode_begin = 0;
		var encode_end = 0;
		var send_begin = 0;
		var send_end = 0;
		var receive_begin = 0;
		var receive_end = 0;
		var reconstructed = 0;
		var decode_begin = 0;
		var decode_end = 0;
		var blit = 0;
		var display = 0;

		if (("encode_begin" in events) && ("send_end" in events))
		{
			encode_begin = events["encode_begin"];
			encode_end   = events["encode_end"];
			send_begin   = events["send_begin"];
			send_end     = events["send_end"];

			var encode = document.createElementNS("http://www.w3.org/2000/svg", 'rect');
			encode.setAttribute('x', encode_begin * t_scale);
			encode.setAttribute('y', y0);
			encode.setAttribute('width', (encode_end - encode_begin) * t_scale);
			encode.setAttribute('height', line_height);
			encode.setAttribute('fill', colours[1]);
			encode.setAttribute('class', 'encode');

			var text1 = document.createElementNS("http://www.w3.org/2000/svg", 'text');
			text1.setAttribute('x', encode_begin * t_scale);
			text1.setAttribute('y', y0 + line_height / 2);
			text1.setAttribute('dominant-baseline', 'middle');
			text1.setAttribute('text-anchor', 'left');
			text1.innerHTML = '+' + encode_begin.toFixed(2) + ' ms';

			var send = document.createElementNS("http://www.w3.org/2000/svg", 'rect');
			send.setAttribute('x', send_begin * t_scale);
			send.setAttribute('y', y0 + line_height * 0.2);
			send.setAttribute('width', (send_end - send_begin) * t_scale);
			send.setAttribute('height', line_height * 0.6);
			send.setAttribute('fill', colours[2]);
			send.setAttribute('class', 'send');

			var text2 = document.createElementNS("http://www.w3.org/2000/svg", 'text');
			text2.setAttribute('x', send_begin * t_scale);
			text2.setAttribute('y', y0 + line_height / 2);
			text2.setAttribute('dominant-baseline', 'middle');
			text2.setAttribute('text-anchor', 'left');
			text2.innerHTML = '+' + send_begin.toFixed(2) + ' ms';

			y0 = y0 + dy;

			g.appendChild(encode);
			g.appendChild(text1);
			g.appendChild(send);
			g.appendChild(text2);
		}
		else
		{
			continue;
		}

		if (("receive_begin" in events) && ("receive_end" in events))
		{
			receive_begin = events["receive_begin"];
			receive_end   = events["receive_end"];

			var receive = document.createElementNS("http://www.w3.org/2000/svg", 'rect');
			receive.setAttribute('x', receive_begin * t_scale);
			receive.setAttribute('y', y0);
			receive.setAttribute('width', Math.max(1, (receive_end - receive_begin) * t_scale));
			receive.setAttribute('height', line_height);
			receive.setAttribute('fill', colours[3]);
			receive.setAttribute('class', 'receive');

			var path = document.createElementNS("http://www.w3.org/2000/svg", 'path');
			const x1 = send_begin * t_scale + 0.5;
			const y1 = y0 - dy + line_height;
			const x2 = receive_begin * t_scale + 0.5;
			const y2 = y0;
			path.setAttribute('d', 'M ' + x1 + ' ' + y1 + ' C ' + x1 + ' ' + y2 + ',' + x2 + ' ' + y1 + ',' + x2 + ' ' + y2);
			path.setAttribute('stroke', '#000000');
			path.setAttribute('fill', 'transparent');

			g.appendChild(receive);
			g.appendChild(path);
		}
		else
		{
			continue;
		}

		if (("decode_begin" in events) && ("decode_end" in events))
		{
			decode_begin = events["decode_begin"];
			decode_end   = events["decode_end"];

			var decode = document.createElementNS("http://www.w3.org/2000/svg", 'rect');
			decode.setAttribute('x', decode_begin * t_scale);
			decode.setAttribute('y', y0);
			decode.setAttribute('width', (decode_end - decode_begin)* t_scale);
			decode.setAttribute('height', line_height);
			decode.setAttribute('fill', colours[5]);
			decode.setAttribute('class', 'decode');

			var text = document.createElementNS("http://www.w3.org/2000/svg", 'text');
			text.setAttribute('x', receive_begin * t_scale);
			text.setAttribute('y', y0 + line_height / 2);
			text.setAttribute('dominant-baseline', 'middle');
			text.setAttribute('text-anchor', 'left');
			text.innerHTML = '+' + receive_begin.toFixed(2) + ' ms';

			y0 = y0 + dy;

			g.appendChild(decode);
			g.appendChild(text);
		}
		else
		{
			continue;
		}

		if ("blit" in events)
		{
			blit = events["blit"];

			var blit_line = document.createElementNS("http://www.w3.org/2000/svg", 'line');
			blit_line.setAttribute('x1', blit * t_scale);
			blit_line.setAttribute('y1', y0);
			blit_line.setAttribute('x2', blit* t_scale);
			blit_line.setAttribute('y2', y0 + line_height);
			blit_line.setAttribute('stroke', colours[6]);
			blit_line.setAttribute('class', 'blit');

			var text = document.createElementNS("http://www.w3.org/2000/svg", 'text');
			text.setAttribute('x', blit * t_scale);
			text.setAttribute('y', y0 + line_height / 2);
			text.setAttribute('dominant-baseline', 'middle');
			text.setAttribute('text-anchor', 'left');
			text.innerHTML = '+' + blit.toFixed(2) + ' ms';

			var path = document.createElementNS("http://www.w3.org/2000/svg", 'path');
			const x1 = decode_end * t_scale - 0.5;
			const y1 = y0 - dy + line_height;
			const x2 = blit * t_scale;
			const y2 = y0;
			path.setAttribute('d', 'M ' + x1 + ' ' + y1 + ' C ' + x1 + ' ' + y2 + ',' + x2 + ' ' + y1 + ',' + x2 + ' ' + y2);
			path.setAttribute('stroke', '#000000');
			path.setAttribute('fill', 'transparent');

			y0 = y0 + dy;

			g.appendChild(blit_line);
			g.appendChild(text);
			g.appendChild(path);
		}
		else
		{
			continue;
		}

		if ("display" in events)
		{
			display = events["display"];

			var display_line = document.createElementNS("http://www.w3.org/2000/svg", 'line');
			display_line.setAttribute('x1', display * t_scale);
			display_line.setAttribute('y1', y0);
			display_line.setAttribute('x2', display* t_scale);
			display_line.setAttribute('y2', y0 + line_height);
			display_line.setAttribute('stroke', colours[6]);
			display_line.setAttribute('class', 'display');

			var text = document.createElementNS("http://www.w3.org/2000/svg", 'text');
			text.setAttribute('x', display * t_scale);
			text.setAttribute('y', y0 + line_height / 2);
			text.setAttribute('dominant-baseline', 'middle');
			text.setAttribute('text-anchor', 'left');
			text.innerHTML = '+' + display.toFixed(2) + ' ms';

			var path = document.createElementNS("http://www.w3.org/2000/svg", 'path');
			const x1 = blit * t_scale;
			const y1 = y0 - dy + line_height;
			const x2 = display * t_scale;
			const y2 = y0;
			path.setAttribute('d', 'M ' + x1 + ' ' + y1 + ' C ' + x1 + ' ' + y2 + ',' + x2 + ' ' + y1 + ',' + x2 + ' ' + y2);
			path.setAttribute('stroke', '#000000');
			path.setAttribute('fill', 'transparent');

			y0 = y0 + dy;

			g.appendChild(display_line);
			g.appendChild(text);
			g.appendChild(path);
		}
		else
		{
			continue;
		}
	}

	svg.appendChild(g);
}

function draw_everything(svg, timing_data)
{
	t0 = 0;       // ms
	t1 = 100;     // ms
	dt = 5;       // ms
	t_offset = 0; // ms
	t_scale = 20; // pix/ms

	line_height = 70;
	line_margin = 50;

	var nb_stream = 0;
	for(let i in timing_data)
	{
		for(let j in timing_data[i])
		{
			if (j.startsWith('stream-'))
				nb_stream = Math.max(nb_stream, parseInt(j.replace('stream-', '')) + 1);
		}
	}
	const dy = line_height + line_margin;
	height = (1 + 4 * nb_stream) * dy;
	svg.setAttribute('height', height);

	// draw_timescale(svg, t0, t1, dt, t_offset, t_scale, height);

	for(let i in timing_data)
	{
		draw_frame(svg, i, timing_data[i], t_offset, t_scale, line_height, line_margin)
	}

	svg.t0 = 0;

	svg.addEventListener('wheel', (event) => {
		event.preventDefault();
		frames = svg.getElementsByClassName('frame');

		svg.t0 = Math.max(0, svg.t0 + event.deltaY * 0.1);
		const t_offset = svg.t0;
		Array.from(frames).forEach((frame) => {
			const t0 = frame.frame_begin;
			frame.setAttribute("transform", "translate(" + ((t0-t_offset) * t_scale) + " 0)");
		});
	});
}
